#define SDL_MAIN_HANDLED  // Ensure SDL handles the main entry point

#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <SDL2/SDL_image.h>
#include <SDL2/SDL_mixer.h>
#include <iostream>
#include <string>
#include <vector>
#include <chrono>
#include <thread>
#include <cmath>
#include <algorithm>
#include <limits>
#include <unordered_map>
#include <fstream>
#include <sstream>
#include <deque>
#include <unordered_set>
#include <cctype>
#include <set>
#include <functional>
#include <iomanip>

// Structure representing a game sprite with rendering and collision properties
struct GameSprite {
    SDL_Rect rect;           // Visual rectangle for rendering
    SDL_Rect footRect;       // Bottom rectangle for depth sorting and collision
    int footW;               // Width of foot rectangle
    int footH;               // Height of foot rectangle
    SDL_Texture* currentTexture;  // Current texture to render
    std::string spriteName;  // Identifier for the sprite
    int currentFrame;        // Current animation frame
    bool isAnimated;         // Flag for animated sprites
    bool isMoving;           // Movement state flag
    bool facingLeft = false; // Direction sprite is facing

    float animAccumulator = 0.0f;  // Time accumulator for animation timing
    std::string currentAnimName;  // Add this field to store current animation name

    // Custom comparison operator for depth sorting
    bool operator<(const GameSprite& other) const {
        // Background always renders first
        if (spriteName == "background") return true;
        if (other.spriteName == "background") return false;
        
        // Sort by bottom position of footRect for pseudo-3D effect
        int thisBottom = footRect.y + footRect.h;
        int otherBottom = other.footRect.y + other.footRect.h;
        
        if (thisBottom != otherBottom) return thisBottom < otherBottom;
        if (footRect.x != other.footRect.x) return footRect.x < other.footRect.x;
        return spriteName < other.spriteName;
    }
};

// Forward declarations for functions
bool loadMapFile(const std::string& mapFilePath);
void updateNPCs();
void debugPlayerAnimation(const GameSprite& sprite);

// Global SDL objects
SDL_Window* window = nullptr;
SDL_Renderer* renderer = nullptr;
TTF_Font* font = nullptr;

// Timing variables for frame management
double deltaTime = 0.0;     // Time since last frame
Uint64 lastFrameTime = 0;   // Timestamp of last frame

// Game world dimensions
const int SCREEN_WIDTH = 800;   // Window width
const int SCREEN_HEIGHT = 600;  // Window height
const int MAP_WIDTH = 1600;     // Total map width
const int MAP_HEIGHT = 1200;    // Total map height
float PLAYER_SPEED = 33.0f;     // Player movement speed

// NPC behavior parameters
const float NPC_SPEED = 55.0f;         // Base NPC movement speed
const float DETECTION_RADIUS = 60.0f; // Player detection range
const float WANDER_CHANGE_TIME = 2.0f; // Time between wander direction changes

// NPC state tracking structure
struct NPCState {
    bool isFollowing = false;   // Following player state
    bool isStationary = false;
    bool isWandering = true;
    float wanderTimer = 0.0f;   // Timer for wander direction
    float wanderAngle = 0.0f;   // Current wander direction angle
};

// Map of NPC states keyed by unique identifiers
std::unordered_map<std::string, NPCState> npcStates;

// Global rendering scale factor
float globalScale = 3.0f;

// Custom comparator for SDL_Rect sorting by Y position
struct CompareSDLRectByY {
    bool operator()(const SDL_Rect& a, const SDL_Rect& b) const {
        if (a.y == b.y) return a.x < b.x;
        return a.y < b.y;
    }
};

// Static rendering assets
std::vector<SDL_Texture*> static_textures;
std::set<SDL_Rect, CompareSDLRectByY> static_texture_rects;

// Animation structure definition
struct animation {
    std::string name;             // Animation name
    std::vector<SDL_Texture*> frames;  // Texture frames
    int frameCount;               // Total frames
    int currentFrame;             // Current frame index
    int frameDelay;               // Delay between frames (ms)
    int footW;                    // Foot rectangle width
    int footH;                    // Foot rectangle height
};

// Game object containers
std::set<GameSprite> gameSprites;  // All active sprites
SDL_Point backgroundOffset = {0, 0};  // Camera offset
SDL_Rect playerRect;                // Player position (deprecated)
SDL_Rect backgroundRect;            // Background position (deprecated)

// Asset storage
std::unordered_map<std::string, SDL_Texture*> textureMap;  // Texture cache
std::unordered_map<std::string, animation> animationMap;   // Animation data
std::unordered_map<std::string, SDL_Point> textureFootMap; // Foot dimensions

// Performance tracking
double currentFPS = 0.0;                     // Current FPS value
std::deque<double> fpsHistory;                // FPS history buffer
const size_t FPS_HISTORY_SIZE = 60;           // History buffer size

// Mouse cursor
GameSprite* cursor = nullptr;  // Custom cursor sprite

// Facing directions
enum class PlayerFacing { N, S, NE, SE, NW, SW };
static PlayerFacing   g_playerFacing = PlayerFacing::S;
static char           g_lastVertical = 'S';  // 'N' or 'S'

// Initialize SDL and create window/renderer
bool initSDL() {
    // Initialize all SDL subsystems
    if (SDL_Init(SDL_INIT_EVERYTHING) < 0) {
        std::cerr << "SDL could not initialize! SDL_Error: " << SDL_GetError() << std::endl;
        return false;
    }

    // Create game window
    window = SDL_CreateWindow("Space Monkeys", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, 800, 600, SDL_WINDOW_SHOWN);
    if (!window) {
        std::cerr << "Window could not be created! SDL_Error: " << SDL_GetError() << std::endl;
        SDL_Quit();
        return false;
    }

    // Create hardware-accelerated renderer with VSync
    renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_PRESENTVSYNC);
    if (!renderer) {
        std::cerr << "Renderer could not be created! SDL Error: " << SDL_GetError() << std::endl;
        SDL_DestroyWindow(window);
        SDL_Quit();
        return false;
    }

    // Initialize image loading support (PNG & JPG)
    if (IMG_Init(IMG_INIT_PNG | IMG_INIT_JPG) == 0) {
        std::cerr << "SDL_image could not initialize! SDL_image Error: " << IMG_GetError() << std::endl;
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return false;
    }

    // Initialize audio mixer
    if (Mix_OpenAudio(44100, MIX_DEFAULT_FORMAT, 2, 2048) < 0) {
        std::cerr << "SDL_mixer could not initialize! SDL_mixer Error: " << Mix_GetError() << std::endl;
        IMG_Quit();
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return false;
    }

    // Initialize font rendering
    if (TTF_Init() == -1) {
        std::cerr << "SDL_ttf could not initialize! SDL_ttf Error: " << TTF_GetError() << std::endl;
        Mix_CloseAudio();
        IMG_Quit();
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return false;
    }

    // Load main font
    font = TTF_OpenFont("assets/fonts/arial.ttf", 16);
    if (!font) {
        std::cerr << "Failed to load font! SDL_ttf Error: " << TTF_GetError() << std::endl;
        // Proceed without font if loading fails
    }

    // Create custom cursor
    SDL_Surface* cursorSurface = IMG_Load("assets/textures/cursor.png");
    if (!cursorSurface) {
        std::cerr << "Failed to load cursor texture! SDL_image Error: " << IMG_GetError() << std::endl;
        return false;
    }

    SDL_Texture* cursorTexture = SDL_CreateTextureFromSurface(renderer, cursorSurface);
    if (!cursorTexture) {
        std::cerr << "Failed to create cursor texture! SDL Error: " << SDL_GetError() << std::endl;
        SDL_FreeSurface(cursorSurface);
        return false;
    }

    int w = cursorSurface->w;
    int h = cursorSurface->h;
    cursor = new GameSprite{{0, 0, w, h}, {0, 0, w, h}, w, h, cursorTexture, "cursor", 0, false, false};
    SDL_FreeSurface(cursorSurface);
    return true;
}

// Render text to the screen
void renderText(const std::string& text, SDL_Color color, int x, int y) {
    if (!font) return; // Skip if no font loaded
    
    // Create text surface
    SDL_Surface* surface = TTF_RenderText_Solid(font, text.c_str(), color);
    if (!surface) {
        std::cerr << "Unable to render text surface! SDL_ttf Error: " << TTF_GetError() << std::endl;
        return;
    }

    // Convert surface to texture
    SDL_Texture* texture = SDL_CreateTextureFromSurface(renderer, surface);
    if (!texture) {
        std::cerr << "Unable to create texture from rendered text! SDL Error: " << SDL_GetError() << std::endl;
        SDL_FreeSurface(surface);
        return;
    }

    // Render texture to screen
    SDL_Rect destRect = {x, y, surface->w, surface->h};
    SDL_RenderCopy(renderer, texture, NULL, &destRect);
    
    // Cleanup resources
    SDL_FreeSurface(surface);
    SDL_DestroyTexture(texture);
}

// Load textures from a manifest file
void loadTexturesFromFile(const std::string& filePath) {
    std::ifstream file(filePath);
    if (!file.is_open()) {
        std::cerr << "Could not open file: " << filePath << std::endl;
        return;
    }

    std::string line;
    while (std::getline(file, line)) {
        // Load image file
        SDL_Surface* surface = IMG_Load(line.c_str());
        if (!surface) {
            std::cerr << "Unable to load texture: " << line << " Error: " << IMG_GetError() << std::endl;
            continue;
        }

        // Create texture from surface
        SDL_Texture* texture = SDL_CreateTextureFromSurface(renderer, surface);
        if (!texture) {
            std::cerr << "Unable to create texture! SDL Error: " << SDL_GetError() << std::endl;
            SDL_FreeSurface(surface);
            continue;
        }

        // Extract texture name from path
        std::string textureName = line.substr(line.find_last_of("/\\") + 1);
        textureName = textureName.substr(0, textureName.find('.'));
        
        // Create sprite with default values
        GameSprite sprite{
            {0, 0, surface->w, surface->h},  // rect
            {0, 0, 32, 32},                  // footRect (default size)
            32,                              // footW
            32,                              // footH
            texture,                         // currentTexture
            textureName,                     // spriteName
            0,                               // currentFrame
            false,                           // isAnimated
            false                            // isMoving
        };
        
        // Add to containers
        gameSprites.insert(sprite);
        static_textures.push_back(texture);
        static_texture_rects.insert({0, 0, surface->w, surface->h});
        SDL_FreeSurface(surface);
    }
}

// Load a game level
bool loadLevel(std::string levelName) {
    // Clear previous level data
    for (auto& texture : static_textures) {
        SDL_DestroyTexture(texture);
    }
    static_textures.clear();
    static_texture_rects.clear();
    gameSprites.clear();
    
    // Build level path and load map
    std::string levelPath = "assets/levels/" + levelName + ".txt";
    return loadMapFile(levelPath);
}

// Process input events and update game state
bool handleEvents() {
    // Find player sprite
    auto playerSprite = std::find_if(gameSprites.begin(), gameSprites.end(),
        [](const GameSprite& sprite) { return sprite.spriteName == "aaron"; });

    if (playerSprite == gameSprites.end()) {
        return true; // Continue if player not found
    }

    // Create modifiable copy of player sprite
    GameSprite updatedSprite = *playerSprite;
    bool needsUpdate = false;

    // Calculate frame timing
    Uint64 currentTime = SDL_GetPerformanceCounter();
    deltaTime = (double)((currentTime - lastFrameTime) * 1000 / (double)SDL_GetPerformanceFrequency()) * 0.001;
    lastFrameTime = currentTime;

    // Process keyboard state
    const Uint8* state = SDL_GetKeyboardState(NULL);
    SDL_ShowCursor(SDL_DISABLE);  // Hide system cursor

    // Exit on ESC key
    if(state[SDL_SCANCODE_ESCAPE]) {
        return false;
    }

    // Calculate movement based on keyboard input
    float moveAmount = PLAYER_SPEED * deltaTime;
    int moveX = 0, moveY = 0;
    bool isMoving = false;

    // Process movement keys
    if (state[SDL_SCANCODE_A]) {  // Left
        moveX -= static_cast<int>(std::round(moveAmount));
        isMoving = true;
    }
    if (state[SDL_SCANCODE_D]) {  // Right
        moveX += static_cast<int>(std::round(moveAmount));
        isMoving = true;
    }
    if (state[SDL_SCANCODE_W]) {  // Up
        moveY -= static_cast<int>(std::round(moveAmount));
        isMoving = true;
    }
    if (state[SDL_SCANCODE_S]) {  // Down
        moveY += static_cast<int>(std::round(moveAmount));
        isMoving = true;
    }

    // Determine facing direction
    bool up    = state[SDL_SCANCODE_W];
    bool down  = state[SDL_SCANCODE_S];
    bool left  = state[SDL_SCANCODE_A];
    bool right = state[SDL_SCANCODE_D];

    // Track last vertical direction
    if (up)   g_lastVertical = 'N';
    if (down) g_lastVertical = 'S';

    PlayerFacing facing;
    
    // Determine facing direction (whether moving or not)
    if      (up  && !left && !right) { facing = PlayerFacing::N; }
    else if (down&& !left && !right) { facing = PlayerFacing::S; }
    else if (up  && right)           { facing = PlayerFacing::NE; }
    else if (up  && left)            { facing = PlayerFacing::NW; }
    else if (down&& right)           { facing = PlayerFacing::SE; }
    else if (down&& left)            { facing = PlayerFacing::SW; }
    else if (right)                  { facing = (g_lastVertical=='N'?PlayerFacing::NE:PlayerFacing::SE); }
    else if (left)                   { facing = (g_lastVertical=='N'?PlayerFacing::NW:PlayerFacing::SW); }
    else                             { facing = g_playerFacing; } // Keep last facing when no input

    // Remember current facing for idle state
    if (isMoving) {
        g_playerFacing = facing;
    }

    // Update player position if moving
    if (isMoving) {
        // Update main rectangle position
        updatedSprite.rect.x += moveX;
        updatedSprite.rect.y += moveY;

        // Clamp to world boundaries
        updatedSprite.rect.x = std::max(0, std::min(MAP_WIDTH - updatedSprite.rect.w, updatedSprite.rect.x));
        updatedSprite.rect.y = std::max(0, std::min(MAP_HEIGHT - updatedSprite.rect.h, updatedSprite.rect.y));

        // Update foot rectangle position
        updatedSprite.footRect.x = updatedSprite.rect.x + (updatedSprite.rect.w - updatedSprite.footW) / 2;
        updatedSprite.footRect.y = updatedSprite.rect.y + updatedSprite.rect.h - updatedSprite.footH;

        // Update camera to follow player
        int cameraX = updatedSprite.rect.x + updatedSprite.rect.w/2 - SCREEN_WIDTH/2;
        int cameraY = updatedSprite.rect.y + updatedSprite.rect.h/2 - SCREEN_HEIGHT/2;

        // Clamp camera to world boundaries
        cameraX = std::max(0, std::min(MAP_WIDTH - SCREEN_WIDTH, cameraX));
        cameraY = std::max(0, std::min(MAP_HEIGHT - SCREEN_HEIGHT, cameraY));

        // Set background offset (negative of camera position)
        backgroundOffset.x = -cameraX;
        backgroundOffset.y = -cameraY;
        
        needsUpdate = true;
    }

    // Always update animation and facing direction
    updatedSprite.isMoving = isMoving;
    
    // Pick animation name based on current state and facing
    std::string animName;
    SDL_RendererFlip flip = SDL_FLIP_NONE;

    if (isMoving) {
        // Walking animations
        switch (facing) {
            case PlayerFacing::N:  animName = "aaronWalkN";    break;
            case PlayerFacing::S:  animName = "aaronWalkS";    break;
            case PlayerFacing::NE: animName = "aaronWalkNE";   break;
            case PlayerFacing::NW: animName = "aaronWalkNE"; flip = SDL_FLIP_HORIZONTAL; break;
            case PlayerFacing::SE: animName = "aaronWalkSE";   break;
            case PlayerFacing::SW: animName = "aaronWalkSE"; flip = SDL_FLIP_HORIZONTAL; break;
        }
    } else {
        // Idle animations
        switch (facing) {
            case PlayerFacing::N:  animName = "aaronIdleN";    break;
            case PlayerFacing::S:  animName = "aaronIdleS";    break;
            case PlayerFacing::NE: animName = "aaronIdleNE";   break;
            case PlayerFacing::NW: animName = "aaronIdleNE";  flip = SDL_FLIP_HORIZONTAL; break;
            case PlayerFacing::SE: animName = "aaronIdleSE";   break;
            case PlayerFacing::SW: animName = "aaronIdleSE";  flip = SDL_FLIP_HORIZONTAL; break;
        }
    }

    // Apply facing flip
    updatedSprite.facingLeft = (flip == SDL_FLIP_HORIZONTAL);

    // Update animation
    auto animIt = animationMap.find(animName);
    if (animIt != animationMap.end()) {
        auto& A = animIt->second;
        
        // Reset animation if switching to new one
        if (updatedSprite.currentAnimName != animName) {
            updatedSprite.currentAnimName = animName;
            updatedSprite.currentFrame = 0;
            updatedSprite.animAccumulator = 0;
            updatedSprite.currentTexture = A.frames[0];
            needsUpdate = true;
        }
        
        // Always update animation frames (use ms for accumulator)
        updatedSprite.animAccumulator += deltaTime * 1000.0f; // deltaTime is seconds, convert to ms
        if (updatedSprite.animAccumulator >= A.frameDelay) {
            updatedSprite.currentFrame = (updatedSprite.currentFrame + 1) % A.frames.size();
            updatedSprite.currentTexture = A.frames[updatedSprite.currentFrame];
            updatedSprite.animAccumulator -= A.frameDelay; // subtract, not reset to 0, for smoothness
            needsUpdate = true;
        }
    } else {
        std::cerr << "Warning: Animation not found for '"<<animName<<"'\n";
    }

    // Update sprite in container if changed
    gameSprites.erase(playerSprite);
    gameSprites.insert(updatedSprite);
    debugPlayerAnimation(updatedSprite);

    // Process event queue
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        switch (event.type) {
            case SDL_QUIT: 
                return false;
            
            case SDL_MOUSEMOTION:
                if (cursor) {
                    cursor->rect.x = event.motion.x;
                    cursor->rect.y = event.motion.y;
                }
                break;

            case SDL_MOUSEBUTTONDOWN:
                if (event.button.button == SDL_BUTTON_LEFT && cursor) {
                    // Update cursor position to the click location
                    cursor->rect.x = event.button.x;
                    cursor->rect.y = event.button.y;

                    SDL_Point mousePoint = { cursor->rect.x, cursor->rect.y };
                    bool found = false;
                    for (auto it = gameSprites.rbegin(); it != gameSprites.rend(); ++it) {
                        const auto& sprite = *it;
                        if (sprite.spriteName == "background" || sprite.spriteName == "cursor")
                            continue;

                        // Apply backgroundOffset and globalScale to get on-screen rect
                        SDL_Rect adjustedRect = {
                            static_cast<int>((sprite.rect.x + backgroundOffset.x) * globalScale),
                            static_cast<int>((sprite.rect.y + backgroundOffset.y) * globalScale),
                            static_cast<int>(sprite.rect.w * globalScale),
                            static_cast<int>(sprite.rect.h * globalScale)
                        };

                        if (SDL_PointInRect(&mousePoint, &adjustedRect)) {
                            std::cout << "Mouse intersects sprite '" << sprite.spriteName
                                      << "' rect: {"
                                      << sprite.rect.x << ", "
                                      << sprite.rect.y << ", "
                                      << sprite.rect.w << ", "
                                      << sprite.rect.h << "}\n";
                            found = true;
                            break; // Only print the topmost sprite
                        }
                    }
                    if (!found) {
                        std::cout << "No sprite under cursor.\n";
                    }
                }
                break;
            case SDL_MOUSEBUTTONUP:
                if (event.button.button == SDL_BUTTON_LEFT && cursor) {
                    // Handle left click release on cursor
                    std::cout << "Cursor released at: (" << cursor->rect.x << ", " << cursor->rect.y << ")\n";
                }
                break;
                
            case SDL_KEYDOWN:
                switch (event.key.keysym.sym) {
                    case SDLK_LEFT:
                        PLAYER_SPEED--;
                        break;
                    case SDLK_RIGHT:
                        PLAYER_SPEED++;
                        break;
                    case SDLK_UP:
                        globalScale *= 1.1f;
                        globalScale = std::min(5.0f, globalScale);
                        break;
                    case SDLK_DOWN:
                        globalScale *= 0.9f;
                        globalScale = std::max(0.1f, globalScale);
                        break;
                }
                break;
        }
    }

    // Update camera based on current player position
    auto currentPlayerSprite = std::find_if(gameSprites.begin(), gameSprites.end(),
        [](const GameSprite& sprite) { return sprite.spriteName == "aaron"; });
    
    if (currentPlayerSprite != gameSprites.end()) {
        int visibleWidth = static_cast<int>(SCREEN_WIDTH / globalScale);
        int visibleHeight = static_cast<int>(SCREEN_HEIGHT / globalScale);
        
        int cameraX = currentPlayerSprite->rect.x + currentPlayerSprite->rect.w/2 - visibleWidth/2;
        int cameraY = currentPlayerSprite->rect.y + currentPlayerSprite->rect.h/2 - visibleHeight/2;
        
        // Clamp camera to world boundaries
        cameraX = std::max(0, std::min(MAP_WIDTH - visibleWidth, cameraX));
        cameraY = std::max(0, std::min(MAP_HEIGHT - visibleHeight, cameraY));
        
        backgroundOffset.x = -cameraX;
        backgroundOffset.y = -cameraY;
    }

    return true;
}

// NEWLY STABLE 

// Debug output for player animation state
void debugPlayerAnimation(const GameSprite& sprite) {
    static std::string lastAnim = "";
    std::string currentAnim = sprite.spriteName;
    
    // Print when animation changes
    if (lastAnim != currentAnim) {
        std::cout << "Player Animation: " << currentAnim;
        if (sprite.isMoving) std::cout << " (Moving)";
        if (sprite.facingLeft) std::cout << " (Facing Left)";
        std::cout << " Frame: " << sprite.currentFrame << std::endl;
        lastAnim = currentAnim;
    }
}

// Load game map from file
bool loadMapFile(const std::string& mapFilePath) {
    std::ifstream file(mapFilePath);
    if (!file.is_open()) {
        std::cerr << "Could not open map file: " << mapFilePath << std::endl;
        return false;
    }

    // Extract level name from path
    std::string levelName;
    size_t lastSlash = mapFilePath.find_last_of("/\\");
    if (lastSlash != std::string::npos) {
        size_t dotPos = mapFilePath.find_last_of('.');
        levelName = mapFilePath.substr(lastSlash + 1, dotPos - lastSlash - 1);
    }

    // Section parsing state
    enum class Section { NONE, TEXTURES, ANIMATIONS, MAP };
    Section currentSection = Section::NONE;
    std::string line;
    std::string texturesPath = "assets/textures/" + levelName + "/";
    std::string animationsPath = "assets/animations/";

    // Process each line of map file
    while (std::getline(file, line)) {
        // Skip empty lines and comments
        if (line.empty() || line[0] == '#') continue;

        // Section headers
        if (line == "[TEXTURES]") currentSection = Section::TEXTURES;
        else if (line == "[ANIMATIONS]") currentSection = Section::ANIMATIONS;
        else if (line == "[MAP]") currentSection = Section::MAP;
        else {
            switch (currentSection) {
                case Section::TEXTURES: {
                    // Parse texture line: "name footW footH"
                    std::istringstream iss(line);
                    std::string textureName;
                    int footW, footH;
                    if (iss >> textureName >> footW >> footH) {
                        std::string texturePath = texturesPath + textureName + ".png";
                        SDL_Surface* surface = IMG_Load(texturePath.c_str());
                        if (!surface) {
                            std::cerr << "Failed to load texture: " << texturePath << std::endl;
                            continue;
                        }
                        // Create texture and store
                        SDL_Texture* texture = SDL_CreateTextureFromSurface(renderer, surface);
                        textureMap[textureName] = texture;
                        textureFootMap[textureName] = {footW, footH};  // Store foot dimensions
                        SDL_FreeSurface(surface);
                    }
                    break;
                }

                case Section::ANIMATIONS: {
                    std::istringstream iss(line);
                    std::string animName;
                    int frameCount, frameDelay, footW, footH;
                    
                    if (iss >> animName >> frameCount >> frameDelay >> footW >> footH) {
                        animation anim;
                        anim.name = animName;
                        anim.frameCount = frameCount;
                        anim.currentFrame = 0;
                        anim.frameDelay = frameDelay;
                        anim.footW = footW;
                        anim.footH = footH;
                        
                        // Extract base folder name (everything before first capital letter)
                        std::string baseFolder = animName;
                        size_t firstCap = 0;
                        while (firstCap < baseFolder.length() && !isupper(baseFolder[firstCap])) {
                            firstCap++;
                        }
                        baseFolder = baseFolder.substr(0, firstCap);  // "aaron" or "mushroom"
                        
                        // Load all frames
                        bool loadedAllFrames = true;
                        for (int i = 1; i <= frameCount; i++) {
                            std::string framePath = animationsPath + baseFolder + "/" + animName + std::to_string(i) + ".png";
                            std::cout << "Loading: " << framePath << std::endl;  // Debug output
                            
                            SDL_Surface* surface = IMG_Load(framePath.c_str());
                            if (!surface) {
                                std::cerr << "Failed to load frame: " << framePath << " - " << IMG_GetError() << std::endl;
                                loadedAllFrames = false;
                                break;
                            }
                            
                            SDL_Texture* texture = SDL_CreateTextureFromSurface(renderer, surface);
                            SDL_FreeSurface(surface);
                            
                            if (!texture) {
                                std::cerr << "Failed to create texture: " << SDL_GetError() << std::endl;
                                loadedAllFrames = false;
                                break;
                            }
                            
                            anim.frames.push_back(texture);
                        }
                        
                        if (loadedAllFrames) {
                            animationMap[animName] = anim;
                            std::cout << "Successfully loaded animation: " << animName << std::endl;  // Debug output
                        } else {
                            // Cleanup partial loads
                            for (auto* tex : anim.frames) {
                                SDL_DestroyTexture(tex);
                            }
                        }
                    }
                    break;
                }

                case Section::MAP: {
                    std::istringstream iss(line);
                    std::string name;
                    int count;
                    if (iss >> name >> count) {
                        // Map base names to initial animations
                        std::string animName = name;
                        if (name == "aaron") animName = "aaronIdleS";
                        else if(name == "reyna") animName = "reynaIdleSE";
                        else if (name == "mushroom") animName = "mushroomHop";

                    

                        SDL_Texture* texture = nullptr;
                        bool isAnim = false;
                        int w = 0, h = 0;
                        int footW = 0, footH = 0;

                        // Try to find animation first
                        if (animationMap.find(animName) != animationMap.end()) {
                            auto& anim = animationMap[animName];
                            if (!anim.frames.empty()) {
                                texture = anim.frames[0];
                                footW = anim.footW;
                                footH = anim.footH;
                                isAnim = true;
                            }
                        } else if (textureMap.find(name) != textureMap.end()) {
                            texture = textureMap[name];
                            auto& footDims = textureFootMap[name];
                            footW = footDims.x;
                            footH = footDims.y;
                        }

                        // Create sprites using original name but initial animation texture
                        if (texture && SDL_QueryTexture(texture, nullptr, nullptr, &w, &h) == 0) {
                            for (int i = 0; i < count; i++) {
                                int x, y;
                                if (iss >> x >> y) {
                                    GameSprite sprite;
                                    sprite.rect = {x, y, w, h};
                                    sprite.currentTexture = texture;
                                    sprite.spriteName = name;
                                    sprite.currentAnimName = animName;  // Store the initial animation name
                                    sprite.isAnimated = isAnim;
                                    sprite.currentFrame = 0;
                                    sprite.isMoving = false;
                                    
                                    // Configure foot rectangle
                                    sprite.footW = footW;
                                    sprite.footH = footH;
                                    sprite.footRect = {
                                        x + (w - footW) / 2,  // Center horizontally
                                        y + h - footH,        // Place at bottom
                                        footW,
                                        footH
                                    };
                                    
                                    // Add to game world
                                    gameSprites.insert(sprite);
                                }
                            }
                        }
                    }
                    break;
                }
                
                case Section::NONE:
                    break;
            }
        }
    }

    return true;
}

// Render game frame
void render() {
    // Clear screen
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderClear(renderer);

    // Render all sprites with scaling and camera offset
    for (const auto& sprite : gameSprites) {
        SDL_Rect adjustedRect = {
            static_cast<int>((sprite.rect.x + backgroundOffset.x) * globalScale),
            static_cast<int>((sprite.rect.y + backgroundOffset.y) * globalScale),
            static_cast<int>(sprite.rect.w * globalScale),
            static_cast<int>(sprite.rect.h * globalScale)
        };
        
        // Apply horizontal flip if needed
        SDL_RendererFlip flip = sprite.facingLeft ? SDL_FLIP_HORIZONTAL : SDL_FLIP_NONE;
        SDL_RenderCopyEx(renderer, sprite.currentTexture, NULL, &adjustedRect, 0, NULL, flip);
    }

    // Render cursor (unscaled)
    if (cursor) {
        SDL_RenderCopy(renderer, cursor->currentTexture, NULL, &cursor->rect);
    }
    
    // Calculate and display FPS
    currentFPS = 1.0 / deltaTime;
    fpsHistory.push_back(currentFPS);
    if (fpsHistory.size() > FPS_HISTORY_SIZE) {
        fpsHistory.pop_front();
    }
    
    double avgFPS = 0;
    for (double fps : fpsHistory) avgFPS += fps;
    avgFPS /= fpsHistory.size();
    
    std::stringstream ss;
    ss << std::fixed << std::setprecision(1) << avgFPS << " FPS";
    renderText(ss.str(), {255, 255, 255, 255}, 10, 10);
    
    // Present final frame
    SDL_RenderPresent(renderer);
}

// Clean up resources
void cleanup() {
    // Clean cursor
    if (cursor) {
        SDL_DestroyTexture(cursor->currentTexture);
        delete cursor;
    }

    // Clean textures
    for (auto& [name, texture] : textureMap) SDL_DestroyTexture(texture);
    for (auto& [name, anim] : animationMap) {
        for (auto& frame : anim.frames) SDL_DestroyTexture(frame);
    }
    for (auto& texture : static_textures) SDL_DestroyTexture(texture);

    // Cleanup subsystems
    TTF_CloseFont(font);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    IMG_Quit();
    Mix_Quit();
    TTF_Quit();
    SDL_Quit();

    std::cout << "Final scale: " << globalScale << std::endl;
}

// Update NPC behavior and position
void updateNPCs() {
    // Find player position
    SDL_Point playerPos{0, 0};
    bool playerFound = false;
    auto pit = std::find_if(gameSprites.begin(), gameSprites.end(),
        [](const GameSprite& s) { return s.spriteName == "aaron"; });
    
    if (pit != gameSprites.end()) {
        // Use center of player's foot rectangle
        playerPos.x = pit->footRect.x + pit->footRect.w/2;
        playerPos.y = pit->footRect.y + pit->footRect.h/2;
        playerFound = true;
    }

    if (!playerFound) return; // Skip if no player

    // Container for updated NPCs
    std::vector<GameSprite> updated;
    auto it = gameSprites.begin();

    while (it != gameSprites.end()) {
        const auto& spr = *it;
        // Skip non-NPCs
        if (spr.spriteName == "aaron" || !spr.isAnimated || spr.spriteName == "background") {
            ++it;
            continue;
        }

        // Create modifiable copy
        GameSprite copy = spr;
        
        // Calculate distance to player
        float npcCenterX = copy.footRect.x + copy.footRect.w/2.0f;
        float npcCenterY = copy.footRect.y + copy.footRect.h/2.0f;
        
        float dx = playerPos.x - npcCenterX;
        float dy = playerPos.y - npcCenterY;
        float dist = std::sqrt(dx*dx + dy*dy);

        // Get NPC state using unique identifier
        std::string npcKey = spr.spriteName + "_" + 
                            std::to_string(spr.rect.x) + "_" + 
                            std::to_string(spr.rect.y);
        auto& st = npcStates[npcKey];

        // Movement calculation
        float mx = 0, my = 0;
        if (spr.spriteName == "reyna") {
            // Reyna is stationary, no movement
            st.isStationary = true;
            st.isWandering = false;
            st.isFollowing = false;
        }

        
        st.isFollowing = (dist < DETECTION_RADIUS && dist > 5.0f);

        if (st.isFollowing && dist > 0) {
            // Chase player with normalized direction
            float dirX = dx / dist;
            float dirY = dy / dist;
            
            mx = dirX * NPC_SPEED * deltaTime;
            my = dirY * NPC_SPEED * deltaTime;
            
            // Slow down when close to player
            if (dist < 50.0f) {
                mx *= 0.5f;
                my *= 0.5f;
            }
        } else if (st.isWandering) {
            float scale = 0.5f;
            // Random wandering
            st.wanderTimer += deltaTime;
            if (st.wanderTimer >= WANDER_CHANGE_TIME) {
                st.wanderAngle = (float(rand() * deltaTime)/RAND_MAX * 2.0f * M_PI);
                st.wanderTimer = 0.0f;
            }
            mx = std::cos(st.wanderAngle) * NPC_SPEED * scale * deltaTime;
            my = std::sin(st.wanderAngle) * NPC_SPEED * scale * deltaTime;
        }
        else if (st.isStationary) {
            // Stationary NPCs do not move
            mx = 0;
            my = 0;
        }

        int newX, newY;

        // Update position with bounds checking
        if(spr.spriteName != "reyna")
        {
            // For non-reyna NPCs, apply movement
            newX = copy.rect.x + static_cast<int>(std::round(mx));
            newY = copy.rect.y + static_cast<int>(std::round(my));
        } else {
            // For Reyna, use a fixed position
            newX = copy.rect.x;
            newY = copy.rect.y;
        }
        
        copy.rect.x = std::clamp(newX, 0, MAP_WIDTH - copy.rect.w);
        copy.rect.y = std::clamp(newY, 0, MAP_HEIGHT - copy.rect.h);
        
        // Update foot rectangle
        copy.footRect.x = copy.rect.x + (copy.rect.w - copy.footW)/2;
        copy.footRect.y = copy.rect.y + copy.rect.h - copy.footH;
        
        /* facingLeft now driven by 8-way logic */
        // Update facing direction
        if (std::abs(mx) > 0.1f) {
            copy.facingLeft = (mx < 0);
        }

        // Update animation
        if (copy.isAnimated) {
            // Use the animation that was set up during map load
            auto animIt = animationMap.find(copy.currentAnimName);  // Use currentAnimName instead of animName
            if (animIt != animationMap.end()) {
                animation& anim = animIt->second;
                
                copy.animAccumulator += deltaTime;
                if (copy.animAccumulator >= (anim.frameDelay / 1000.0)) {
                    copy.currentFrame = (copy.currentFrame + 1) % anim.frames.size();
                    copy.currentTexture = anim.frames[copy.currentFrame];
                    copy.animAccumulator = 0;
                }
            }
        }

        // Queue for reinsertion
        updated.push_back(copy);
        it = gameSprites.erase(it);
    }

    // Reinsert updated NPCs
    for (auto& s : updated) {
        gameSprites.insert(std::move(s));
    }
}

// Main game loop
int main(int argc, char* argv[]) {
    // Initialize systems
    if (!initSDL()) return -1;
    if (!loadLevel("level1")) return -1;

    // Initialize timing
    lastFrameTime = SDL_GetPerformanceCounter();
    bool running = true;
    
    // Main game loop
    while (running) {
        running = handleEvents();
        updateNPCs();
        render();
    }

    // Cleanup and exit
    cleanup();

    for(auto a: npcStates)
    {
        std::cout << a.first << std::endl;
    }
    return 0;
}
