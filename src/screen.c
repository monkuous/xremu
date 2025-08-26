#ifdef EMSCRIPTEN
#include <emscripten.h>
#endif

#include <SDL.h>
#include <getopt.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "screen.h"
#include "xr.h"

struct Screen Screens[MAXSCREENS];

struct Screen *ScreenCurrent;
int ScreenCurrentID = 0;
int ScreenNextID = 0;

int WindowWidth = 0;
int WindowHeight = 0;

int ScreenZoom = 1;
int ScreenFirstDraw = 1;

bool ScreenMouseGrabbed = false;

SDL_Window *ScreenWindow;
SDL_Renderer *ScreenRenderer;

SDL_Rect WindowRect;

void KinnowDump();

void ScreenInit() {
	// all the screens should have been created before this is called.

	SDL_Rect bounds;
	if (SDL_GetDisplayBounds(0, &bounds) == 0 &&
	    bounds.h >= WindowHeight * 2 && bounds.w >= WindowWidth * 2) {
	  ScreenZoom = 2;
	} else {
	  ScreenZoom = 1;
	}

	ScreenWindow = SDL_CreateWindow("XR/station Emulator",
										SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
										(int)(WindowWidth * ScreenZoom),
										(int)(WindowHeight * ScreenZoom),
										SDL_WINDOW_HIDDEN | SDL_WINDOW_ALLOW_HIGHDPI);

	if (!ScreenWindow) {
		fprintf(stderr, "failed to create window\n");
		exit(1);
	}

	ScreenRenderer = SDL_CreateRenderer(ScreenWindow, -1, 0);

	if (ScreenRenderer == NULL) {
		fprintf(stderr, "failed to create renderer");
		exit(1);
	}

	SDL_RenderSetLogicalSize(ScreenRenderer, WindowWidth, WindowHeight);

	WindowRect = (SDL_Rect){
		.w = WindowWidth,
		.h = WindowHeight,
	};

	ScreenCurrent = &Screens[0];
}

void ScreenDraw() {
	ScreenCurrent->Draw(ScreenCurrent);

	ScreenCurrent->FirstDraw = 0;

	SDL_Rect screenrect = {
		.w = ScreenCurrent->Width,
		.h = ScreenCurrent->Height,
	};

	SDL_Rect winrect = {
		.w = ScreenCurrent->Width,
		.h = ScreenCurrent->Height,
		.x = 0,
		.y = 0
	};

	if ((WindowRect.w != screenrect.w) || (WindowRect.h != screenrect.h)) {
		int oldx;
		int oldy;

		SDL_GetWindowPosition(ScreenWindow, &oldx, &oldy);

		oldx += (WindowRect.w - screenrect.w)/2;
		oldy += (WindowRect.h - screenrect.h)/2;

		SDL_SetWindowSize(ScreenWindow, screenrect.w * ScreenZoom, screenrect.h * ScreenZoom);
		SDL_SetWindowPosition(ScreenWindow, oldx, oldy);

		WindowRect.w = screenrect.w;
		WindowRect.h = screenrect.h;

		SDL_RenderSetLogicalSize(ScreenRenderer, WindowRect.w, WindowRect.h);
	}

	SDL_RenderClear(ScreenRenderer);
	SDL_RenderCopy(ScreenRenderer, ScreenCurrent->Texture, &screenrect, &winrect);
	SDL_RenderPresent(ScreenRenderer);

	if (ScreenFirstDraw) {
		SDL_ShowWindow(ScreenWindow);
		ScreenFirstDraw = 0;
	}
}

void ScreenNext() {
	ScreenCurrentID++;

	if (ScreenCurrentID >= ScreenNextID)
		ScreenCurrentID = 0;

	ScreenCurrent = &Screens[ScreenCurrentID];
}

void ScreenPrev() {
	ScreenCurrentID--;

	if (ScreenCurrentID < 0)
		ScreenCurrentID = ScreenNextID-1;

	ScreenCurrent = &Screens[ScreenCurrentID];
}

bool IsAltDown = false;

int ScreenProcessEvents() {
	SDL_Event event;
	while (SDL_PollEvent(&event)) {
		switch (event.type) {
			case SDL_QUIT: {
#ifdef EMSCRIPTEN
				emscripten_cancel_main_loop();
#endif
				return 1;
			}

			case SDL_WINDOWEVENT: {
				break;
			}

			case SDL_MOUSEMOTION: {
				if (ScreenMouseGrabbed) {
					if (ScreenCurrent->MouseMoved)
						ScreenCurrent->MouseMoved(ScreenCurrent, event.motion.xrel, event.motion.yrel);
				}
				break;
			}

			case SDL_MOUSEBUTTONDOWN: {
				if (!ScreenMouseGrabbed) {
					SDL_SetWindowGrab(ScreenWindow, true);
					SDL_ShowCursor(false);
					SDL_SetWindowTitle(ScreenWindow, "XR/station - strike F12 to uncapture mouse");
					SDL_SetRelativeMouseMode(true);
					ScreenMouseGrabbed = true;
					break;
				}

				if (ScreenCurrent->MousePressed)
					ScreenCurrent->MousePressed(ScreenCurrent, event.button.button);
				break;
			}


			case SDL_MOUSEBUTTONUP: {
				if (ScreenCurrent->MouseReleased)
					ScreenCurrent->MouseReleased(ScreenCurrent, event.button.button);
				break;
			}

			case SDL_KEYDOWN:
				if ((event.key.keysym.scancode == SDL_SCANCODE_F12) && ScreenMouseGrabbed) {
					SDL_SetWindowGrab(ScreenWindow, false);
					SDL_ShowCursor(true);
					SDL_SetWindowTitle(ScreenWindow, "XR/station");
					SDL_SetRelativeMouseMode(false);
					ScreenMouseGrabbed = false;
					break;
				} else if (event.key.keysym.scancode == SDL_SCANCODE_RALT) {
					ScreenNext();
					break;
				} else if (event.key.keysym.scancode == SDL_SCANCODE_LALT) {
					IsAltDown = true;
				} else if (event.key.keysym.scancode == SDL_SCANCODE_TAB && IsAltDown) {
					// alt-tab means NMI
					XrProcessorTable[0]->UserBreak = 1;
				} else if (event.key.keysym.scancode == SDL_SCANCODE_F1 && IsAltDown) {
					// alt-f1 means screenshot
					KinnowDump();
				}

				if (ScreenCurrent->KeyPressed)
					ScreenCurrent->KeyPressed(ScreenCurrent, event.key.keysym.scancode);
				break;

			case SDL_KEYUP:
				if (event.key.keysym.scancode == SDL_SCANCODE_LALT) {
					IsAltDown = false;
				}

				if (ScreenCurrent->KeyReleased)
					ScreenCurrent->KeyReleased(ScreenCurrent, event.key.keysym.scancode);
				break;
		}
	}

	return 0;
}

struct SDL_Texture *ScreenGetTexture(struct Screen *screen) {
	if (screen->Texture) {
		return screen->Texture;
	}

	screen->Texture = SDL_CreateTexture(ScreenRenderer,
										SDL_PIXELFORMAT_ARGB8888,
										SDL_TEXTUREACCESS_STREAMING,
										screen->Width,
										screen->Height);

	return screen->Texture;
}

struct Screen *ScreenCreate(int w, int h, char *title,
							ScreenDrawF draw,
							ScreenKeyPressedF keypressed,
							ScreenKeyReleasedF keyreleased,
							ScreenMousePressedF mousepressed,
							ScreenMouseReleasedF mousereleased,
							ScreenMouseMovedF mousemoved) {

	if (ScreenNextID >= MAXSCREENS) {
		fprintf(stderr, "maximum screens reached\n");
		exit(1);
	}

	struct Screen *screen = &Screens[ScreenNextID++];

	if (w > WindowWidth)
		WindowWidth = w;

	if (h > WindowHeight)
		WindowHeight = h;

	screen->Width = w;
	screen->Height = h;
	screen->Title = title;
	screen->FirstDraw = 1;

	screen->Draw = draw;
	screen->KeyPressed = keypressed;
	screen->KeyReleased = keyreleased;
	screen->MousePressed = mousepressed;
	screen->MouseReleased = mousereleased;
	screen->MouseMoved = mousemoved;

	return screen;
}