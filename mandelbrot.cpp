#include <iostream>
#include <complex>
#include <SDL2/SDL.h>
#include <thread>
#include <atomic>
#include <mutex>
#include <sstream>
#include <iomanip>
#include <cstring>

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

// Simple 5x7 font for digits and basic chars
const unsigned char FONT_5X7[][7] = {
    {0x0E,0x11,0x13,0x15,0x19,0x11,0x0E}, // 0
    {0x04,0x0C,0x04,0x04,0x04,0x04,0x0E}, // 1
    {0x0E,0x11,0x01,0x02,0x04,0x08,0x1F}, // 2
    {0x1F,0x02,0x04,0x02,0x01,0x11,0x0E}, // 3
    {0x02,0x06,0x0A,0x12,0x1F,0x02,0x02}, // 4
    {0x1F,0x10,0x1E,0x01,0x01,0x11,0x0E}, // 5
    {0x06,0x08,0x10,0x1E,0x11,0x11,0x0E}, // 6
    {0x1F,0x01,0x02,0x04,0x08,0x08,0x08}, // 7
    {0x0E,0x11,0x11,0x0E,0x11,0x11,0x0E}, // 8
    {0x0E,0x11,0x11,0x0F,0x01,0x02,0x0C}, // 9
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00}, // space (10)
    {0x00,0x00,0x00,0x1F,0x00,0x00,0x00}, // - (11)
    {0x00,0x00,0x00,0x00,0x00,0x0C,0x0C}, // . (12)
    {0x00,0x04,0x00,0x00,0x00,0x04,0x00}, // : (13)
    {0x00,0x11,0x0A,0x04,0x0A,0x11,0x00}, // X (14)
    {0x00,0x11,0x11,0x0A,0x04,0x04,0x00}, // Y (15)
    {0x0E,0x11,0x11,0x11,0x11,0x11,0x0E}, // O (16 - for 'Done')
    {0x1E,0x11,0x11,0x11,0x11,0x11,0x1E}, // D (17)
    {0x11,0x19,0x15,0x13,0x11,0x11,0x11}, // N (18 - for 'Done')
    {0x1F,0x10,0x10,0x1E,0x10,0x10,0x1F}, // E (19)
    {0x1E,0x11,0x11,0x1E,0x10,0x10,0x10}, // P (20 - for 'Pass')
    {0x0E,0x11,0x11,0x1F,0x11,0x11,0x11}, // A (21)
    {0x0E,0x11,0x10,0x0E,0x01,0x11,0x0E}, // S (22)
    {0x1F,0x01,0x02,0x04,0x08,0x10,0x1F}, // Z (23 - for 'Zoom')
    {0x11,0x1B,0x15,0x15,0x11,0x11,0x11}, // M (24)
    {0x01,0x02,0x04,0x08,0x10,0x00,0x00}, // / (25)
    {0x00,0x00,0x04,0x0E,0x04,0x00,0x00}, // + (26 - for exponent)
    {0x04,0x0A,0x11,0x11,0x11,0x0A,0x04}, // o (27 - degree/times symbol as x)
    {0x04,0x04,0x04,0x04,0x04,0x00,0x04}, // ! (28)
    {0x00,0x04,0x0E,0x15,0x04,0x04,0x04}, // up arrow (29) - unused
    {0x0E,0x01,0x01,0x0E,0x01,0x01,0x0E}, // [ (30)
    {0x0E,0x10,0x10,0x0E,0x10,0x10,0x0E}, // ] (31)
};

int charToIndex(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    switch(c) {
        case ' ': return 10;
        case '-': return 11;
        case '.': return 12;
        case ':': return 13;
        case 'X': case 'x': return 14;
        case 'Y': case 'y': return 15;
        case 'O': case 'o': return 16;
        case 'D': case 'd': return 17;
        case 'N': case 'n': return 18;
        case 'E': case 'e': return 19;
        case 'P': case 'p': return 20;
        case 'A': case 'a': return 21;
        case 'S': case 's': return 22;
        case 'Z': case 'z': return 23;
        case 'M': case 'm': return 24;
        case '/': return 25;
        case '+': return 26;
        case '*': return 27;
        case '!': return 28;
        case '[': return 30;
        case ']': return 31;
        default: return 10; // space
    }
}

void drawChar(Uint32* pixels, int startX, int startY, char c, Uint32 color, int scale = 2) {
    int idx = charToIndex(c);
    for (int row = 0; row < 7; row++) {
        unsigned char rowData = FONT_5X7[idx][row];
        for (int col = 0; col < 5; col++) {
            if (rowData & (0x10 >> col)) {
                for (int sy = 0; sy < scale; sy++) {
                    for (int sx = 0; sx < scale; sx++) {
                        int px = startX + col * scale + sx;
                        int py = startY + row * scale + sy;
                        if (px >= 0 && px < WIDTH && py >= 0 && py < HEIGHT) {
                            pixels[py * WIDTH + px] = color;
                        }
                    }
                }
            }
        }
    }
}

void drawString(Uint32* pixels, int x, int y, const std::string& str, Uint32 color, int scale = 2) {
    int charWidth = 6 * scale;
    for (size_t i = 0; i < str.length(); i++) {
        drawChar(pixels, x + i * charWidth, y, str[i], color, scale);
    }
}

void drawRect(Uint32* pixels, int x, int y, int w, int h, Uint32 color) {
    for (int py = y; py < y + h && py < HEIGHT; py++) {
        for (int px = x; px < x + w && px < WIDTH; px++) {
            if (px >= 0 && py >= 0) {
                // Alpha blend
                Uint32 existing = pixels[py * WIDTH + px];
                int er = (existing >> 16) & 0xFF;
                int eg = (existing >> 8) & 0xFF;
                int eb = existing & 0xFF;
                int nr = (color >> 16) & 0xFF;
                int ng = (color >> 8) & 0xFF;
                int nb = color & 0xFF;
                int alpha = (color >> 24) & 0xFF;
                int r = (nr * alpha + er * (255 - alpha)) / 255;
                int g = (ng * alpha + eg * (255 - alpha)) / 255;
                int b = (nb * alpha + eb * (255 - alpha)) / 255;
                pixels[py * WIDTH + px] = (255 << 24) | (r << 16) | (g << 8) | b;
            }
        }
    }
}

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

void progressiveRender(Uint32* pixels, double targetXMin, double targetXMax,
                       double targetYMin, double targetYMax) {
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
    }

    renderInProgress.store(false);
}

void zoom(int mouseX, int mouseY, double factor) {
    // Get the complex coordinate under the mouse
    double clickReal = xMin + (xMax - xMin) * mouseX / WIDTH;
    double clickImag = yMin + (yMax - yMin) * mouseY / HEIGHT;

    // Calculate new range
    double newXRange = (xMax - xMin) * factor;
    double newYRange = (yMax - yMin) * factor;

    // Keep the clicked point at the same screen position
    // The mouse is at (mouseX/WIDTH, mouseY/HEIGHT) fraction of the screen
    double mouseXFrac = (double)mouseX / WIDTH;
    double mouseYFrac = (double)mouseY / HEIGHT;

    xMin = clickReal - newXRange * mouseXFrac;
    xMax = clickReal + newXRange * (1.0 - mouseXFrac);
    yMin = clickImag - newYRange * mouseYFrac;
    yMax = clickImag + newYRange * (1.0 - mouseYFrac);
}

void reset() {
    xMin = -2.5;
    xMax = 1.0;
    yMin = -1.5;
    yMax = 1.5;
}

std::string formatDouble(double val) {
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(6) << val;
    return oss.str();
}

std::string formatZoom(double zoom) {
    std::ostringstream oss;
    if (zoom >= 1000000) {
        oss << std::fixed << std::setprecision(1) << (zoom / 1000000) << "M";
    } else if (zoom >= 1000) {
        oss << std::fixed << std::setprecision(1) << (zoom / 1000) << "k";
    } else {
        oss << std::fixed << std::setprecision(1) << zoom;
    }
    return oss.str();
}

void drawUI(Uint32* uiPixels, Uint32* fractalPixels) {
    // Copy fractal to UI buffer
    memcpy(uiPixels, fractalPixels, WIDTH * HEIGHT * sizeof(Uint32));

    // Draw semi-transparent background
    drawRect(uiPixels, 10, 10, 320, 75, 0xA0000000);

    Uint32 textColor = 0xFFCCCCCC;
    int y = 16;
    int lineHeight = 18;

    // Pass info
    std::string passStr;
    if (renderInProgress.load()) {
        passStr = "Pass " + std::to_string(currentPass.load()) + "/" + std::to_string(totalPasses.load());
    } else {
        passStr = "Done!";
    }
    drawString(uiPixels, 16, y, passStr, textColor);

    // X range
    y += lineHeight;
    std::string xStr = "X [" + formatDouble(xMin) + " " + formatDouble(xMax) + "]";
    drawString(uiPixels, 16, y, xStr, textColor);

    // Y range
    y += lineHeight;
    std::string yStr = "Y [" + formatDouble(yMin) + " " + formatDouble(yMax) + "]";
    drawString(uiPixels, 16, y, yStr, textColor);

    // Zoom level
    y += lineHeight;
    double zoomLevel = 3.5 / (xMax - xMin);
    std::string zoomStr = "Zoom " + formatZoom(zoomLevel) + "x";
    drawString(uiPixels, 16, y, zoomStr, textColor);
}

int main() {
    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        std::cerr << "SDL init failed: " << SDL_GetError() << std::endl;
        return 1;
    }

    SDL_Window* window = SDL_CreateWindow(
        "Mandelbrot - Left:zoom in, Right:zoom out, R:reset, Q:quit",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        WIDTH, HEIGHT, SDL_WINDOW_SHOWN
    );

    if (!window) {
        std::cerr << "Window creation failed: " << SDL_GetError() << std::endl;
        SDL_Quit();
        return 1;
    }

    SDL_Renderer* renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
    SDL_Texture* texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888,
                                              SDL_TEXTUREACCESS_STREAMING, WIDTH, HEIGHT);

    Uint32* fractalPixels = new Uint32[WIDTH * HEIGHT];
    Uint32* uiPixels = new Uint32[WIDTH * HEIGHT];
    std::thread* renderThread = nullptr;

    renderThread = new std::thread(progressiveRender, fractalPixels, xMin, xMax, yMin, yMax);

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

                    renderThread = new std::thread(progressiveRender, fractalPixels,
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
                        renderThread = new std::thread(progressiveRender, fractalPixels,
                                                       xMin, xMax, yMin, yMax);
                    } else if (event.key.keysym.sym == SDLK_q ||
                               event.key.keysym.sym == SDLK_ESCAPE) {
                        running = false;
                    }
                    break;
            }
        }

        // Draw UI overlay and update display
        drawUI(uiPixels, fractalPixels);
        SDL_UpdateTexture(texture, NULL, uiPixels, WIDTH * sizeof(Uint32));
        SDL_RenderClear(renderer);
        SDL_RenderCopy(renderer, texture, NULL, NULL);
        SDL_RenderPresent(renderer);

        SDL_Delay(16);
    }

    renderInterrupted.store(true);
    if (renderThread && renderThread->joinable()) {
        renderThread->join();
        delete renderThread;
    }

    delete[] fractalPixels;
    delete[] uiPixels;
    SDL_DestroyTexture(texture);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();

    return 0;
}
