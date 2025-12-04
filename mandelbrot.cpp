#include <iostream>
#include <complex>
#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <thread>
#include <atomic>
#include <mutex>
#include <sstream>
#include <iomanip>

const int WIDTH = 1920;
const int HEIGHT = 1080;
const int MAX_ITERATIONS = 1000;

double xMin = -2.5, xMax = 1.0;
double yMin = -1.5, yMax = 1.5;

std::atomic<bool> renderInterrupted(false);
std::atomic<bool> renderInProgress(false);
std::atomic<int> currentPass(0);
std::atomic<int> totalPasses(6);
std::mutex renderMutex;

int mandelbrot(std::complex<double> c) {
    std::complex<double> z = 0;
    int iterations = 0;

    while (std::abs(z) <= 2.0 && iterations < MAX_ITERATIONS) {
        z = z * z + c;
        iterations++;
    }

    return iterations;
}

void getColor(int iterations, Uint8& r, Uint8& g, Uint8& b) {
    if (iterations == MAX_ITERATIONS) {
        r = g = b = 0;
    } else {
        double t = (double)iterations / MAX_ITERATIONS;
        r = (Uint8)(9 * (1 - t) * t * t * t * 255);
        g = (Uint8)(15 * (1 - t) * (1 - t) * t * t * 255);
        b = (Uint8)(8.5 * (1 - t) * (1 - t) * (1 - t) * t * 255);
    }
}

bool renderPass(Uint32* pixels, int blockSize, double currentXMin, double currentXMax,
                double currentYMin, double currentYMax) {
    for (int y = 0; y < HEIGHT; y += blockSize) {
        for (int x = 0; x < WIDTH; x += blockSize) {
            if (renderInterrupted.load()) {
                return false;
            }

            double real = currentXMin + (currentXMax - currentXMin) * (x + blockSize/2.0) / WIDTH;
            double imag = currentYMin + (currentYMax - currentYMin) * (y + blockSize/2.0) / HEIGHT;

            std::complex<double> c(real, imag);
            int iterations = mandelbrot(c);

            Uint8 r, g, b;
            getColor(iterations, r, g, b);
            Uint32 color = (255 << 24) | (r << 16) | (g << 8) | b;

            for (int by = 0; by < blockSize && (y + by) < HEIGHT; by++) {
                for (int bx = 0; bx < blockSize && (x + bx) < WIDTH; bx++) {
                    pixels[(y + by) * WIDTH + (x + bx)] = color;
                }
            }
        }
    }
    return true;
}

void progressiveRender(SDL_Renderer* renderer, SDL_Texture* texture, Uint32* pixels,
                       double targetXMin, double targetXMax, double targetYMin, double targetYMax) {
    std::lock_guard<std::mutex> lock(renderMutex);
    renderInProgress.store(true);
    renderInterrupted.store(false);

    int blockSizes[] = {32, 16, 8, 4, 2, 1};
    int numPasses = 6;
    totalPasses.store(numPasses);

    for (int pass = 0; pass < numPasses; pass++) {
        if (renderInterrupted.load()) {
            break;
        }

        currentPass.store(pass + 1);
        int blockSize = blockSizes[pass];

        if (!renderPass(pixels, blockSize, targetXMin, targetXMax, targetYMin, targetYMax)) {
            break;
        }

        SDL_UpdateTexture(texture, NULL, pixels, WIDTH * sizeof(Uint32));
    }

    renderInProgress.store(false);
}

void zoom(int mouseX, int mouseY, double factor) {
    double clickReal = xMin + (xMax - xMin) * mouseX / WIDTH;
    double clickImag = yMin + (yMax - yMin) * mouseY / HEIGHT;

    double xRange = (xMax - xMin) * factor;
    double yRange = (yMax - yMin) * factor;

    xMin = clickReal - xRange / 2;
    xMax = clickReal + xRange / 2;
    yMin = clickImag - yRange / 2;
    yMax = clickImag + yRange / 2;
}

void reset() {
    xMin = -2.5;
    xMax = 1.0;
    yMin = -1.5;
    yMax = 1.5;
}

std::string formatDouble(double val, int precision = 6) {
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(precision) << val;
    return oss.str();
}

void renderText(SDL_Renderer* renderer, TTF_Font* font, const std::string& text, int x, int y, SDL_Color color) {
    SDL_Surface* surface = TTF_RenderText_Blended(font, text.c_str(), color);
    if (!surface) return;

    SDL_Texture* texture = SDL_CreateTextureFromSurface(renderer, surface);
    if (!texture) {
        SDL_FreeSurface(surface);
        return;
    }

    SDL_Rect rect = {x, y, surface->w, surface->h};
    SDL_RenderCopy(renderer, texture, NULL, &rect);

    SDL_DestroyTexture(texture);
    SDL_FreeSurface(surface);
}

void renderUI(SDL_Renderer* renderer, TTF_Font* font, SDL_Texture* fractalTexture) {
    SDL_RenderClear(renderer);
    SDL_RenderCopy(renderer, fractalTexture, NULL, NULL);

    SDL_Color textColor = {200, 200, 200, 255};
    SDL_Color bgColor = {0, 0, 0, 180};

    // Build info strings
    std::string passStr = "Pass: " + std::to_string(currentPass.load()) + "/" + std::to_string(totalPasses.load());
    if (!renderInProgress.load()) {
        passStr = "Done";
    }

    std::string xStr = "X: [" + formatDouble(xMin) + ", " + formatDouble(xMax) + "]";
    std::string yStr = "Y: [" + formatDouble(yMin) + ", " + formatDouble(yMax) + "]";

    double zoomLevel = 3.5 / (xMax - xMin);  // 3.5 is initial width
    std::string zoomStr = "Zoom: " + formatDouble(zoomLevel, 1) + "x";

    // Draw semi-transparent background for text
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 160);
    SDL_Rect bgRect = {8, 8, 380, 90};
    SDL_RenderFillRect(renderer, &bgRect);

    // Render text
    int yPos = 12;
    int lineHeight = 20;
    renderText(renderer, font, passStr, 14, yPos, textColor);
    renderText(renderer, font, xStr, 14, yPos + lineHeight, textColor);
    renderText(renderer, font, yStr, 14, yPos + lineHeight * 2, textColor);
    renderText(renderer, font, zoomStr, 14, yPos + lineHeight * 3, textColor);

    SDL_RenderPresent(renderer);
}

int main() {
    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        std::cerr << "SDL init failed: " << SDL_GetError() << std::endl;
        return 1;
    }

    if (TTF_Init() < 0) {
        std::cerr << "TTF init failed: " << TTF_GetError() << std::endl;
        SDL_Quit();
        return 1;
    }

    SDL_Window* window = SDL_CreateWindow(
        "Mandelbrot Set - Left click: zoom in, Right click: zoom out, R: reset, Q: quit",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        WIDTH, HEIGHT, SDL_WINDOW_SHOWN
    );

    if (!window) {
        std::cerr << "Window creation failed: " << SDL_GetError() << std::endl;
        TTF_Quit();
        SDL_Quit();
        return 1;
    }

    SDL_Renderer* renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
    SDL_Texture* texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888,
                                              SDL_TEXTUREACCESS_STREAMING, WIDTH, HEIGHT);

    // Load a system font
    TTF_Font* font = TTF_OpenFont("/System/Library/Fonts/SFNSMono.ttf", 16);
    if (!font) {
        font = TTF_OpenFont("/System/Library/Fonts/Monaco.ttf", 14);
    }
    if (!font) {
        font = TTF_OpenFont("/System/Library/Fonts/Menlo.ttc", 14);
    }
    if (!font) {
        std::cerr << "Could not load font: " << TTF_GetError() << std::endl;
        SDL_DestroyTexture(texture);
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        TTF_Quit();
        SDL_Quit();
        return 1;
    }

    Uint32* pixels = new Uint32[WIDTH * HEIGHT];
    std::thread* renderThread = nullptr;

    renderThread = new std::thread(progressiveRender, renderer, texture, pixels, xMin, xMax, yMin, yMax);

    bool running = true;
    SDL_Event event;

    while (running) {
        while (SDL_PollEvent(&event)) {
            switch (event.type) {
                case SDL_QUIT:
                    running = false;
                    break;

                case SDL_MOUSEBUTTONDOWN: {
                    renderInterrupted.store(true);
                    if (renderThread && renderThread->joinable()) {
                        renderThread->join();
                        delete renderThread;
                    }

                    if (event.button.button == SDL_BUTTON_LEFT) {
                        zoom(event.button.x, event.button.y, 0.5);
                    } else if (event.button.button == SDL_BUTTON_RIGHT) {
                        zoom(event.button.x, event.button.y, 2.0);
                    }

                    renderThread = new std::thread(progressiveRender, renderer, texture, pixels,
                                                   xMin, xMax, yMin, yMax);
                    break;
                }

                case SDL_KEYDOWN:
                    if (event.key.keysym.sym == SDLK_r) {
                        renderInterrupted.store(true);
                        if (renderThread && renderThread->joinable()) {
                            renderThread->join();
                            delete renderThread;
                        }
                        reset();
                        renderThread = new std::thread(progressiveRender, renderer, texture, pixels,
                                                       xMin, xMax, yMin, yMax);
                    } else if (event.key.keysym.sym == SDLK_q ||
                               event.key.keysym.sym == SDLK_ESCAPE) {
                        running = false;
                    }
                    break;
            }
        }

        // Update UI every frame
        renderUI(renderer, font, texture);
        SDL_Delay(16);
    }

    renderInterrupted.store(true);
    if (renderThread && renderThread->joinable()) {
        renderThread->join();
        delete renderThread;
    }

    delete[] pixels;
    TTF_CloseFont(font);
    SDL_DestroyTexture(texture);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    TTF_Quit();
    SDL_Quit();

    return 0;
}
