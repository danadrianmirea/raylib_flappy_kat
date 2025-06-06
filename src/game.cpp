#include <vector>
#include <utility>
#include <string>
#include <cmath>  // For sqrtf
#include <algorithm> // For std::remove_if
#include <fstream>

#include "raylib.h"
#include "globals.h"
#include "game.h"

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

//#define DEBUG

bool Game::isMobile = false;

Game::Game(int width, int height)
{
    firstTimeGameStart = true;

    // Initialize audio device
    InitAudioDevice();

    // Initialize Flappy Bird variables
    playerSize = 80.0f;
    playerX = width / 4;
    playerY = height / 2;
    playerVelocity = 0.0f;
    gravity = defaultGravity;
    jumpForce = defaultJumpForce;
    pipeWidth = defaultPipeWidth;
    pipeGap = defaultPipeGap;
    pipeSpeed = defaultPipeSpeed;
    basePipeSpeed = pipeSpeed;  // Store initial speed
    pipeSpawnInterval = defaultPipeSpawnInterval;
    pipeSpawnTimer = pipeSpawnInterval;  // Initialize to spawn interval to trigger immediate spawn
    initialPipeDistance = basePipeSpeed * pipeSpawnInterval;  // Store initial distance between pipes
    speedLevel = 0;             // Start at level 0

    // Initialize sounds
    gameMusic = LoadMusicStream("Data/music.mp3");
    SetMusicVolume(gameMusic, 0.15f); 
    flySound = LoadSound("Data/fly.mp3");
    hitSound = LoadSound("Data/hit.mp3");
    scoreSound = LoadSound("Data/ding.mp3");
    musicPlaying = false;  // Start with music off
    musicManuallyDisabled = false;  // Initialize as not manually disabled
    // Don't start music immediately, wait for game to begin

    // Initialize score
    score = 0;
    LoadHighScore();

    playerCollisionWidthRatio = 0.70f;
    playerCollisionHeightRatio = 0.55f;

#ifdef __EMSCRIPTEN__
    // Check if we're running on a mobile device
    isMobile = EM_ASM_INT({
        return /Android|webOS|iPhone|iPad|iPod|BlackBerry|IEMobile|Opera Mini/i.test(navigator.userAgent);
    });
#endif

    targetRenderTex = LoadRenderTexture(gameScreenWidth, gameScreenHeight);
    SetTextureFilter(targetRenderTex.texture, TEXTURE_FILTER_BILINEAR);

    font = LoadFontEx("Font/monogram.ttf", 128, 0, 0);
    SetTextureFilter(font.texture, TEXTURE_FILTER_BILINEAR);

    this->width = width;
    this->height = height;

    // Background initialization
    backgroundTexture = LoadTexture("Data/background.jpg");
    backgroundScrollX = 0.0f;
    backgroundScrollSpeed = basePipeSpeed * 0.2f;  // Set initial scroll speed to 20% of pipe speed
    playerTexture = LoadTexture("Data/redkat_eyes_open.png");
    playerTextureEyesClosed = LoadTexture("Data/redkat_eyes_closed.png");
    playerEyesClosedTimer = 0.0f;
    InitGame();

    pipeTexture = LoadTexture("Data/pipe.png");
}

Game::~Game()
{
    UnloadRenderTexture(targetRenderTex);
    UnloadFont(font);

    // Unload background texture
    UnloadTexture(backgroundTexture);

    // Unload sounds
    UnloadMusicStream(gameMusic);
    UnloadSound(flySound);
    UnloadSound(hitSound);
    UnloadSound(scoreSound);
    UnloadTexture(playerTexture);
    UnloadTexture(playerTextureEyesClosed);
    UnloadTexture(pipeTexture);
    // Close audio device
    CloseAudioDevice();
}

void Game::InitGame()
{
    isInExitMenu = false;
    paused = false;
    lostWindowFocus = false;
    gameOver = false;

    screenScale = MIN((float)GetScreenWidth() / gameScreenWidth, (float)GetScreenHeight() / gameScreenHeight);
}

void Game::Reset()
{
    InitGame();
    // Reset player position and velocity
    playerX = width / 4;
    playerY = height / 2;
    playerVelocity = 0.0f;
    // Clear all pipes
    pipes.clear();
    pipeSpawnTimer = 0.0f;
    pipeSpawnInterval = 2.0f;
    // Reset score and speed
    score = 0;
    speedLevel = 0;
    pipeSpeed = basePipeSpeed;
    
    // Only restart music if it wasn't manually disabled
    if (!musicManuallyDisabled) {
        PlayMusicStream(gameMusic);
        musicPlaying = true;
    }
}

void Game::Update(float dt)
{
    if (dt == 0)
    {
        return;
    }

    screenScale = MIN((float)GetScreenWidth() / gameScreenWidth, (float)GetScreenHeight() / gameScreenHeight);
    bool skipFrame = UpdateUI();
    if(skipFrame) {
        return;
    }

    bool running = (firstTimeGameStart == false && paused == false && lostWindowFocus == false && isInExitMenu == false && gameOver == false);

    // Only scroll background when running
    if (running) {
        backgroundScrollX += backgroundScrollSpeed * dt;
        if (backgroundScrollX >= backgroundTexture.width)
            backgroundScrollX -= backgroundTexture.width;
    }

    if (musicPlaying) {
        UpdateMusicStream(gameMusic);
    }

    if (running)
    {
        HandleInput();

        UpdatePipeSpeed(dt);
        
        // Update player physics
        playerVelocity += gravity * dt;
        playerY += playerVelocity * dt;

        // Calculate collision box dimensions
        float collisionBoxWidth = playerSize * playerCollisionWidthRatio;
        float collisionBoxHeight = playerSize * playerCollisionHeightRatio;

        // Check for collisions with screen boundaries using collision box
        if (playerY - collisionBoxHeight/2 < 0 || playerY + collisionBoxHeight/2 > height) {
            gameOver = true;
            gameOverDelayTimer = gameOverDelayDuration; // Initialize delay timer
            // Stop all sounds before playing hit sound
            StopMusicStream(gameMusic);
            StopSound(flySound);
            StopSound(scoreSound);
            PlaySound(hitSound);
            if (score > highScore) {
                highScore = score;
                SaveHighScore();
            }
        }

        // Update pipes
        pipeSpawnTimer += dt;
        if (pipeSpawnTimer >= pipeSpawnInterval) {
            pipeSpawnTimer = 0.0f;
            
            // Calculate the target gap center based on the previous pipe
            float targetGapCenter;
            if (pipes.empty()) {
                // First pipe - place it in the middle
                targetGapCenter = height / 2;
            } else {
                // Get the previous pipe's gap center
                float prevGapCenter = pipes.back().gapCenter;
                
                // Calculate the minimum and maximum allowed gap center
                float minGapCenter = MAX(pipeGap/2, prevGapCenter - maxGapHeightDifference);
                float maxGapCenter = MIN(height - pipeGap/2, prevGapCenter + maxGapHeightDifference);
                
                // Randomly choose a new gap center within the allowed range
                targetGapCenter = GetRandomValue(minGapCenter, maxGapCenter);
            }
            
            pipes.push_back({(float)width, targetGapCenter, false});
        }

        // Move pipes and check collisions
        for (auto& pipe : pipes) {
            pipe.x -= pipeSpeed * dt;
            // Check if player has passed the pipe
            if (playerX > pipe.x + pipeWidth && !pipe.scored) {
                score++;
                pipe.scored = true;
                PlaySound(scoreSound);
                if (score > highScore) {
                    highScore = score;
                    SaveHighScore();
                }
            }

            // Check collision with pipe using collision box
            if (!gameOver) {
                // Check if player is within pipe's x range
                if (playerX + collisionBoxWidth/2 > pipe.x && playerX - collisionBoxWidth/2 < pipe.x + pipeWidth) {
                    // Check if player is outside the gap
                    if (playerY - collisionBoxHeight/2 < pipe.gapCenter - pipeGap/2 || 
                        playerY + collisionBoxHeight/2 > pipe.gapCenter + pipeGap/2) {
                        gameOver = true;
                        gameOverDelayTimer = gameOverDelayDuration; // Initialize delay timer
                        // Stop all sounds before playing hit sound
                        StopMusicStream(gameMusic);
                        StopSound(flySound);
                        StopSound(scoreSound);
                        PlaySound(hitSound);
                        if (score > highScore) {
                            highScore = score;
                            SaveHighScore();
                        }
                    }
                }
            }
        }

        // Remove pipes that are off screen
        pipes.erase(std::remove_if(pipes.begin(), pipes.end(), 
            [this](const auto& pipe) { return pipe.x < -this->pipeWidth; }), 
            pipes.end());

        if (playerEyesClosedTimer > 0.0f) {
            playerEyesClosedTimer -= dt;
            if (playerEyesClosedTimer < 0.0f) playerEyesClosedTimer = 0.0f;
        }
    }

    // Handle game over restart
    if (gameOver) {
        // Update game over delay timer
        if (gameOverDelayTimer > 0.0f) {
            gameOverDelayTimer -= dt;
            if (gameOverDelayTimer < 0.0f) gameOverDelayTimer = 0.0f;
        }
        
        // Only allow restart input after delay has passed
        if (gameOverDelayTimer <= 0.0f) {
            if (isMobile) {
                if (IsGestureDetected(GESTURE_TAP)) {
                    Reset();
                }
            } else if (IsKeyPressed(KEY_ENTER)) {
                Reset();
            }
        }
    }
}

void Game::HandleInput()
{
    // Only handle flap input if the game is running and not paused
    if (!paused && !gameOver && !firstTimeGameStart && !isInExitMenu && !lostWindowFocus) {
        // Flap on keyboard or mobile tap
        if (IsKeyPressed(KEY_SPACE) || IsKeyPressed(KEY_UP) || IsKeyPressed(KEY_W)
            || (isMobile && IsGestureDetected(GESTURE_TAP)))
        {
            playerVelocity = jumpForce;
            PlaySound(flySound);
            playerEyesClosedTimer = playerEyesClosedDuration;
        }
    }

    // Handle music toggle with M key
    if (IsKeyPressed(KEY_M)) {
        if (musicPlaying) {
            PauseMusicStream(gameMusic);
            musicPlaying = false;
            musicManuallyDisabled = true;  // Player manually disabled music
        } else {
            PlayMusicStream(gameMusic);
            musicPlaying = true;
            musicManuallyDisabled = false;  // Player manually enabled music
        }
    }
}

bool Game::UpdateUI()
{
#ifndef EMSCRIPTEN_BUILD
    if (WindowShouldClose() || (IsKeyPressed(KEY_ESCAPE) && exitWindowRequested == false))
    {
        exitWindowRequested = true;
        isInExitMenu = true;
        return false;
    }

    if (IsKeyPressed(KEY_ENTER) && (IsKeyDown(KEY_LEFT_ALT) || IsKeyDown(KEY_RIGHT_ALT)))
    {
        if (fullscreen)
        {
            fullscreen = false;
            ToggleBorderlessWindowed();
        }
        else
        {
            fullscreen = true;
            ToggleBorderlessWindowed();
        }
    }
#endif

    if(firstTimeGameStart) {
        if(isMobile) {
            if(IsGestureDetected(GESTURE_TAP)) {
                firstTimeGameStart = false;
                // Start music when game begins
                PlayMusicStream(gameMusic);
                musicPlaying = true;
            }
        }
        else if(IsKeyDown(KEY_ENTER)) {
            firstTimeGameStart = false;
            // Start music when game begins
            PlayMusicStream(gameMusic);
            musicPlaying = true;
        }
    }

    if (exitWindowRequested)
    {
        if (IsKeyPressed(KEY_Y))
        {
            exitWindow = true;
        }
        else if (IsKeyPressed(KEY_N) || IsKeyPressed(KEY_ESCAPE))
        {
            exitWindowRequested = false;
            isInExitMenu = false;
        }
    }

    if (IsWindowFocused() == false)
    {
        lostWindowFocus = true;
    }
    else
    {
        lostWindowFocus = false;
    }

#ifndef EMSCRIPTEN_BUILD
    if (exitWindowRequested == false && lostWindowFocus == false && gameOver == false && IsKeyPressed(KEY_P))
#else
    if (exitWindowRequested == false && lostWindowFocus == false && gameOver == false && (IsKeyPressed(KEY_P) || IsKeyPressed(KEY_ESCAPE)))
#endif
    {
        paused = !paused;
    }

    // Handle pausing/unpausing on mobile with tap
    if (isMobile && !firstTimeGameStart && !gameOver && !exitWindowRequested) {
        if (!paused && IsGestureDetected(GESTURE_TAP)) {
            // Get tap position in screen space
            Vector2 tapPos = GetTouchPosition(0);
            
            // Transform tap position from screen space to game space
            float screenOffsetX = (GetScreenWidth() - ((float)gameScreenWidth * screenScale)) * 0.5f;
            float screenOffsetY = (GetScreenHeight() - ((float)gameScreenHeight * screenScale)) * 0.5f;
            
            // Convert screen coordinates to game coordinates
            tapPos.x = (tapPos.x - screenOffsetX) / screenScale;
            tapPos.y = (tapPos.y - screenOffsetY) / screenScale;
            
            // Create a rectangle at the top of the screen
            Rectangle titleArea = {0, 0, (float)width, 100};
            // Check if tap is within the title area
            if (CheckCollisionPointRec(tapPos, titleArea)) {
                paused = true;
                return true;
            }
        } else if (paused && IsGestureDetected(GESTURE_TAP)) {
            paused = false;
            return true;
        }
    }
    return false;
}

void Game::Draw()
{
    // render everything to a texture
    BeginTextureMode(targetRenderTex);

    // Draw scrolling background (revert to original logic)
    float srcX = backgroundScrollX;
    float srcWidth = (float)gameScreenWidth;
    if (srcX + srcWidth <= backgroundTexture.width) {
        // No wrap needed
        DrawTexturePro(
            backgroundTexture,
            { srcX, 0, srcWidth, (float)gameScreenHeight },
            { 0, 0, srcWidth, (float)gameScreenHeight },
            { 0, 0 }, 0.0f, WHITE
        );
    } else {
        // Wrap around
        float firstPart = backgroundTexture.width - srcX;
        DrawTexturePro(
            backgroundTexture,
            { srcX, 0, firstPart, (float)gameScreenHeight },
            { 0, 0, firstPart, (float)gameScreenHeight },
            { 0, 0 }, 0.0f, WHITE
        );
        DrawTexturePro(
            backgroundTexture,
            { 0, 0, srcWidth - firstPart, (float)gameScreenHeight },
            { firstPart, 0, srcWidth - firstPart, (float)gameScreenHeight },
            { 0, 0 }, 0.0f, WHITE
        );
    }

    // Draw pipes with graphics
    for (const auto& pipe : pipes) {
        float topPipeHeight = pipe.gapCenter - pipeGap/2;
        float bottomPipeY = pipe.gapCenter + pipeGap/2;
        float bottomPipeHeight = height - bottomPipeY;

        int capHeight = 24; // Set this to the cap height in your image
        int pipeImgWidth = pipeTexture.width;
        int pipeImgHeight = pipeTexture.height;
        int bodyHeight = pipeImgHeight - capHeight;

        // Draw top pipe (flipped vertically)
        if (topPipeHeight > 0) {
            // Draw body (stretched)
            float bodyDrawHeight = topPipeHeight - capHeight;
            if (bodyDrawHeight > 0) {
                DrawTexturePro(
                    pipeTexture,
                    { 0, (float)capHeight, (float)pipeImgWidth, (float)bodyHeight },
                    { pipe.x, 0, pipeWidth, bodyDrawHeight },
                    { 0, 0 }, 0.0f, WHITE
                );
            }
            // Draw cap (flipped)
            DrawTexturePro(
                pipeTexture,
                { 0, 0, (float)pipeImgWidth, (float)capHeight },
                { pipe.x, bodyDrawHeight, pipeWidth, (float)capHeight },
                { 0, 0 }, 0.0f, WHITE
            );
        }

        // Draw bottom pipe (normal)
        if (bottomPipeHeight > 0) {
            // Draw body (stretched)
            float bodyDrawHeight = bottomPipeHeight - capHeight;
            if (bodyDrawHeight > 0) {
                DrawTexturePro(
                    pipeTexture,
                    { 0, (float)capHeight, (float)pipeImgWidth, (float)bodyHeight },
                    { pipe.x, bottomPipeY + (float)capHeight, pipeWidth, bodyDrawHeight },
                    { 0, 0 }, 0.0f, WHITE
                );
            }
            // Draw cap (normal)
            DrawTexturePro(
                pipeTexture,
                { 0, 0, (float)pipeImgWidth, (float)capHeight },
                { pipe.x, bottomPipeY, pipeWidth, (float)capHeight },
                { 0, 0 }, 0.0f, WHITE
            );
        }
    }

    // Choose player texture:
    Texture2D currentPlayerTexture;
    if (gameOver) {
        // If crashed, always show eyes closed
        currentPlayerTexture = playerTextureEyesClosed;
    } else if (playerEyesClosedTimer > 0.0f) {
        // If flapping, show eyes closed
        currentPlayerTexture = playerTextureEyesClosed;
    } else {
        // Otherwise, show eyes open
        currentPlayerTexture = playerTexture;
    }

    DrawTexturePro(
        currentPlayerTexture,
        { 0, 0, (float)currentPlayerTexture.width, (float)currentPlayerTexture.height },
        { playerX - playerSize/2, playerY - playerSize/2, playerSize, playerSize },
        { 0, 0 }, 0.0f, WHITE
    );

#ifdef DEBUG
    // Draw player collision box for debugging (red outline)
    float collisionBoxWidth = playerSize * playerCollisionWidthRatio;
    float collisionBoxHeight = playerSize * playerCollisionHeightRatio;
    DrawRectangleLines(
        (int)(playerX - collisionBoxWidth/2),
        (int)(playerY - collisionBoxHeight/2),
        (int)collisionBoxWidth,
        (int)collisionBoxHeight,
        RED
    );
#endif
    DrawUI();

    EndTextureMode();

    // render the scaled frame texture to the screen
    BeginDrawing();
    ClearBackground(BLACK);
    DrawTexturePro(targetRenderTex.texture, 
        (Rectangle){0.0f, 0.0f, (float)targetRenderTex.texture.width, (float)-targetRenderTex.texture.height},
        (Rectangle){(GetScreenWidth() - ((float)gameScreenWidth * screenScale)) * 0.5f, (GetScreenHeight() - ((float)gameScreenHeight * screenScale)) * 0.5f, (float)gameScreenWidth * screenScale, (float)gameScreenHeight * screenScale},
        (Vector2){0, 0}, 0.0f, WHITE);
    EndDrawing();
}

void Game::DrawUI()
{
    float screenX = 0.0f;
    float screenY = 0.0f;

    if(isMobile) {
        // Draw pause rectangle area at the top of the screen
        Color grayTransparent = {128, 128, 128, 8}; // Semi-transparent gray
        DrawRectangle(0, 0, gameScreenWidth, 100, grayTransparent);
        
        // Draw centered "Tap to pause" text
        const char* text = "Tap to pause";
        int fontSize = 20;
        int textWidth = MeasureText(text, fontSize);
        DrawText(text, (gameScreenWidth - textWidth)/2, 40, fontSize, BLACK);
    }

    // Draw score on the right side
    std::string scoreText = "Score: " + std::to_string(score);
    std::string highScoreText = "High Score: " + std::to_string(highScore);
    std::string speedText = "Speed: " + std::to_string((int)pipeSpeed);
    int scoreWidth = MeasureText(scoreText.c_str(), 20);
    int highScoreWidth = MeasureText(highScoreText.c_str(), 20);
    int speedWidth = MeasureText(speedText.c_str(), 20);
    int rightPadding = 20;
    
    DrawText(scoreText.c_str(), width - scoreWidth - rightPadding, 20, 20, BLACK);
    DrawText(highScoreText.c_str(), width - highScoreWidth - rightPadding, 50, 20, BLACK);
    DrawText(speedText.c_str(), width - speedWidth - rightPadding, 80, 20, BLACK);

    if(!isMobile) {
        // Draw music toggle instruction at the bottom
        const char* musicText = "Press M to toggle music";
        int musicTextWidth = MeasureText(musicText, 20);
        DrawText(musicText, (gameScreenWidth - musicTextWidth)/2, gameScreenHeight - 30, 20, BLACK);
    }

    if (exitWindowRequested)
    {
        DrawRectangleRounded({screenX + (float)(gameScreenWidth / 2 - 250), screenY + (float)(gameScreenHeight / 2 - 20), 500, 60}, 0.76f, 20, BLACK);
        DrawText("Are you sure you want to exit? [Y/N]", screenX + (gameScreenWidth / 2 - 200), screenY + gameScreenHeight / 2, 20, yellow);
    }
    else if (firstTimeGameStart)
    {
        DrawRectangleRounded(
            {screenX + (float)(gameScreenWidth / 2 - 320), screenY + (float)(gameScreenHeight / 2 - 130), 700, 300},
            0.76f, 20, BLACK
        );

        // Welcome and instructions
        int y = (int)(screenY + (gameScreenHeight / 2 - 110));
        DrawText("Welcome to Hovercat", (int)(screenX + (gameScreenWidth / 2 - 260)), y, 20, yellow);
        y += 40;
        DrawText("Controls:", (int)(screenX + (gameScreenWidth / 2 - 260)), y, 20, yellow);
        y += 30;
        if(!isMobile) {
            DrawText("- Press [Space], [W] or [Up Arrow] to flap", (int)(screenX + (gameScreenWidth / 2 - 220)), y, 20, WHITE);
            y += 30;
#ifndef EMSCRIPTEN_BUILD
            DrawText("- Press [P] to pause", (int)(screenX + (gameScreenWidth / 2 - 220)), y, 20, WHITE);
            y += 30;
            DrawText("- Press [Esc] to exit", (int)(screenX + (gameScreenWidth / 2 - 220)), y, 20, WHITE);
            y += 30;
            DrawText("- Press [M] to toggle music", (int)(screenX + (gameScreenWidth / 2 - 220)), y, 20, WHITE);
            y += 40;
            DrawText("Press Enter to play", (int)(screenX + (gameScreenWidth / 2 - 100)), y, 20, yellow);
            y += 30;
            DrawText("Alt+Enter: toggle fullscreen", (int)(screenX + (gameScreenWidth / 2 - 120)), y, 20, yellow);
#else
            DrawText("- Press [P] or [ESC] to pause", (int)(screenX + (gameScreenWidth / 2 - 220)), y, 20, WHITE);
            y += 30;
            DrawText("- Press [M] to toggle music", (int)(screenX + (gameScreenWidth / 2 - 220)), y, 20, WHITE);
            y += 70;
            DrawText("Press Enter to play", (int)(screenX + (gameScreenWidth / 2 - 100)), y, 20, yellow);        
#endif
        } else {
            DrawText("- Tap to flap", (int)(screenX + (gameScreenWidth / 2 - 220)), y, 20, WHITE);
            y += 30;
            DrawText("- Tap title bar to pause", (int)(screenX + (gameScreenWidth / 2 - 220)), y, 20, WHITE);  
            y += 70;
            DrawText("Tap to play", (int)(screenX + (gameScreenWidth / 2 - 100)), y, 20, yellow);
        }
    }
    else if (paused)
    {
        DrawRectangleRounded({screenX + (float)(gameScreenWidth / 2 - 250), screenY + (float)(gameScreenHeight / 2 - 20), 500, 60}, 0.76f, 20, BLACK);
#ifndef EMSCRIPTEN_BUILD
        DrawText("Game paused, press P to continue", screenX + (gameScreenWidth / 2 - 200), screenY + gameScreenHeight / 2, 20, yellow);
#else
        if (isMobile) {
            DrawText("Game paused, tap to continue", screenX + (gameScreenWidth / 2 - 200), screenY + gameScreenHeight / 2, 20, yellow);
        } else {
            DrawText("Game paused, press P or ESC to continue", screenX + (gameScreenWidth / 2 - 200), screenY + gameScreenHeight / 2, 20, yellow);
        }
#endif
    }
    else if (lostWindowFocus)
    {
        DrawRectangleRounded({screenX + (float)(gameScreenWidth / 2 - 250), screenY + (float)(gameScreenHeight / 2 - 20), 500, 60}, 0.76f, 20, BLACK);
        DrawText("Game paused, focus window to continue", screenX + (gameScreenWidth / 2 - 200), screenY + gameScreenHeight / 2, 20, yellow);
    }
    else if (gameOver)
    {
        DrawRectangleRounded({screenX + (float)(gameScreenWidth / 2 - 250), screenY + (float)(gameScreenHeight / 2 - 20), 500, 100}, 0.76f, 20, BLACK);
        std::string gameOverText = "Game Over! Score: " + std::to_string(score);
        int gameOverTextWidth = MeasureText(gameOverText.c_str(), 20);
        DrawText(gameOverText.c_str(), screenX + (gameScreenWidth / 2 - gameOverTextWidth/2), screenY + gameScreenHeight / 2 - 10, 20, yellow);
        if (isMobile) {
            DrawText("Tap to play again", screenX + (gameScreenWidth / 2 - 100), screenY + gameScreenHeight / 2 + 30, 20, yellow);
        } else {
            DrawText("Press Enter to play again", screenX + (gameScreenWidth / 2 - 120), screenY + gameScreenHeight / 2 + 30, 20, yellow);
        }
    }
}

std::string Game::FormatWithLeadingZeroes(int number, int width)
{
    std::string numberText = std::to_string(number);
    int leadingZeros = width - numberText.length();
    numberText = std::string(leadingZeros, '0') + numberText;
    return numberText;
}

void Game::Randomize()
{
}

void Game::LoadHighScore()
{
#ifndef __EMSCRIPTEN__
    std::ifstream file("highscore.txt");
    if (file.is_open()) {
        file >> highScore;
        file.close();
    } else {
        highScore = 0;
    }
#else
    highScore = 0;
#endif
}

void Game::SaveHighScore()
{
#ifndef __EMSCRIPTEN__
    std::ofstream file("highscore.txt");
    if (file.is_open()) {
        file << highScore;
        file.close();
    }
#endif
}

void Game::UpdatePipeSpeed(float dt)
{
    pipeSpeed += pipeSpeedIncrease * dt;  // Smooth speed increase over time 
    if (pipeSpeed > maxSpeed) {
        pipeSpeed = maxSpeed;
    }
    pipeSpawnInterval = initialPipeDistance / pipeSpeed; // Adjust spawn interval to maintain constant distance between pipes
    backgroundScrollSpeed = pipeSpeed * 0.2f;  // Update background scroll speed to 20% of current pipe speed
    
    // Scale gravity and jump force with pipe speed
    //float speedRatio = pipeSpeed / basePipeSpeed;  // How much faster we are compared to base speed
    //gravity = defaultGravity * speedRatio;  // Scale gravity with speed
    //jumpForce = defaultJumpForce * speedRatio;  // Scale jump force with speed
}