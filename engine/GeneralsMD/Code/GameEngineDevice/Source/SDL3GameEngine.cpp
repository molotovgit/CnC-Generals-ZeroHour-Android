/*
**	Command & Conquer Generals Zero Hour(tm)
**	Copyright 2025 Electronic Arts Inc.
**
**	This program is free software: you can redistribute it and/or modify
**	it under the terms of the GNU General Public License as published by
**	the Free Software Foundation, either version 3 of the License, or
**	(at your option) any later version.
**
**	This program is distributed in the hope that it will be useful,
**	but WITHOUT ANY WARRANTY; without even the implied warranty of
**	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
**	GNU General Public License for more details.
**
**	You should have received a copy of the GNU General Public License
**	along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

/*
** SDL3GameEngine.cpp
**
** Linux implementation of GameEngine using SDL3 for windowing/input.
**
** TheSuperHackers @feature CnC_Generals_Linux 07/02/2026
** Provides SDL3-based input and window management for Linux builds.
** Based on fighter19 reference implementation.
*/

#ifndef _WIN32

#include "SDL3GameEngine.h"
// GeneralsX @build Mr. Meeseeks 16/06/2026 Make audio headers mutually exclusive to avoid redefinition conflicts
#ifdef SAGE_USE_MINIAUDIO
#include "MiniAudioDevice/MiniAudioManager.h"
#elif defined(SAGE_USE_OPENAL)
#include "OpenALAudioManager.h"
#endif
#include "SDL3Device/GameClient/SDL3Mouse.h"
#include "SDL3Device/GameClient/SDL3Keyboard.h"
#include "GameClient/Mouse.h"
#include "GameClient/Keyboard.h"
#include "GameClient/GameWindow.h"
#include "GameClient/GameWindowManager.h"
#include "GameClient/Gadget.h"
#include "W3DDevice/GameLogic/W3DGameLogic.h"
#include "W3DDevice/GameClient/W3DGameClient.h"
#include "W3DDevice/Common/W3DModuleFactory.h"
#include "W3DDevice/Common/W3DThingFactory.h"
#include "W3DDevice/Common/W3DFunctionLexicon.h"
#include "W3DDevice/Common/W3DRadar.h"
#include "W3DDevice/GameClient/W3DParticleSys.h"
#include "W3DDevice/GameClient/W3DWebBrowser.h"
#include "StdDevice/Common/StdLocalFileSystem.h"
#include "StdDevice/Common/StdBIGFileSystem.h"
#include "Common/GlobalData.h"
#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
// GeneralsX @feature Android touch → RTS controls: camera/view + game-mode gating
#if defined(__ANDROID__)
#include "GameClient/View.h"
#include "GameLogic/GameLogic.h"
#include "GameClient/InGameUI.h"
#include "GameClient/Display.h"
#include "GameClient/VideoPlayer.h"
#endif

// Extern globals for input devices (set by GameClient)
extern Mouse *TheMouse;
extern Keyboard *TheKeyboard;
extern GameWindowManager *TheWindowManager;

namespace {

Bool DecodeNextUtf8Codepoint(const char* text, size_t length, size_t& offset, UnsignedInt& outCodepoint)
{
	outCodepoint = 0;
	if (!text || offset >= length) {
		return false;
	}

	const unsigned char first = static_cast<unsigned char>(text[offset]);
	if (first == 0) {
		return false;
	}

	if (first < 0x80) {
		outCodepoint = first;
		offset += 1;
		return true;
	}

	if ((first & 0xE0) == 0xC0 && offset + 1 < length) {
		const unsigned char second = static_cast<unsigned char>(text[offset + 1]);
		if ((second & 0xC0) == 0x80) {
			outCodepoint = ((first & 0x1F) << 6) | (second & 0x3F);
			offset += 2;
			return true;
		}
	}

	if ((first & 0xF0) == 0xE0 && offset + 2 < length) {
		const unsigned char second = static_cast<unsigned char>(text[offset + 1]);
		const unsigned char third = static_cast<unsigned char>(text[offset + 2]);
		if ((second & 0xC0) == 0x80 && (third & 0xC0) == 0x80) {
			outCodepoint = ((first & 0x0F) << 12) | ((second & 0x3F) << 6) | (third & 0x3F);
			offset += 3;
			return true;
		}
	}

	if ((first & 0xF8) == 0xF0 && offset + 3 < length) {
		const unsigned char second = static_cast<unsigned char>(text[offset + 1]);
		const unsigned char third = static_cast<unsigned char>(text[offset + 2]);
		const unsigned char fourth = static_cast<unsigned char>(text[offset + 3]);
		if ((second & 0xC0) == 0x80 && (third & 0xC0) == 0x80 && (fourth & 0xC0) == 0x80) {
			outCodepoint = ((first & 0x07) << 18) | ((second & 0x3F) << 12) | ((third & 0x3F) << 6) | (fourth & 0x3F);
			offset += 4;
			return true;
		}
	}

	// Invalid UTF-8 sequence: skip one byte and keep processing.
	offset += 1;
	return false;
}

}

#if defined(__ANDROID__)
// ============================================================================
// GeneralsX @feature Android touch → RTS controls
// SDL's single-finger→mouse synthesis is disabled (SDL_HINT_TOUCH_MOUSE_EVENTS
// "0" in init()); we synthesize precise input here instead:
//     1 finger tap    -> left click   (select / press a menu button)
//     1 finger drag   -> left drag    (drag-box select / move a slider)
//     2 finger tap    -> right click  (move / attack command)
//     2 finger drag   -> pan camera   (View::userScrollBy)
//     2 finger pinch  -> zoom camera  (View::userZoom)
// The first finger's left-button-down is deferred a few ms so we can tell a
// 1-finger press apart from the START of a 2-finger gesture — that way a
// 2-finger gesture never emits a spurious left click (which would deselect
// units right before a move order, or fire a menu button).
// ============================================================================
extern View     *TheTacticalView;
extern GameLogic *TheGameLogic;
// In-game ESC/pause menu toggle (QuitMenu.cpp) — same action the desktop ESC key runs.
extern void ToggleQuitMenu();

namespace {

struct TouchFinger { SDL_FingerID id; float sx, sy, x, y; };

TouchFinger  g_tf[2];
int          g_tfCount = 0;

// deferred single-finger left button
bool         g_leftPending = false;   // 1st finger down; left-down not yet committed
bool         g_leftHeld    = false;   // a left-button-down was synthesized and is held
SDL_FingerID g_leftId      = 0;
float        g_leftSX = 0, g_leftSY = 0;
Uint64       g_leftDownMs  = 0;

// two-finger gesture
bool         g_two = false;
bool         g_twoMoved = false;
float        g_lastCx = 0, g_lastCy = 0, g_lastDist = 0;
float        g_startCx = 0, g_startCy = 0;
Uint64       g_twoStartMs = 0;

// after a 2-finger gesture begins, ignore leftover fingers for the left button
// until ALL fingers lift (stops a stray drag-box when one finger lifts first)
bool         g_suppressLeft = false;

// on-screen ESC/menu button (top-left). Tapping it toggles the in-game pause menu.
// KEEP THIS GEOMETRY IN SYNC WITH kMenuBtn* IN W3DInGameUI.cpp (the draw side).
const float  MENU_BTN_X = 16, MENU_BTN_Y = 14, MENU_BTN_W = 108, MENU_BTN_H = 56;
bool         g_escArmed = false;      // first finger came down on the menu button
SDL_FingerID g_escFingerId = 0;

// on-screen SKIP button (top-right), shown only during a video/cutscene. Tapping it
// synthesizes ESC → the movie loops skip. Right-anchored; KEEP IN SYNC WITH W3DDisplay.cpp.
const float  SKIP_BTN_W = 150, SKIP_BTN_H = 54, SKIP_BTN_MX = 20, SKIP_BTN_Y = 18;
bool         g_skipArmed = false;
SDL_FingerID g_skipFingerId = 0;

// tuning
const Uint64 TAP_DEFER_MS   = 45;     // defer left-down this long to catch a 2nd finger
const float  MOVE_THRESH_PX = 14.0f;  // finger travel that turns a tap into a drag
const Uint64 TWO_TAP_MAX_MS = 300;    // 2-finger press shorter than this w/o move = right-click
const float  PAN_K          = 1.0f;   // scroll offset per pixel of 2-finger centroid travel
const float  PINCH_ZOOM_K   = 0.08f;  // zoom height per pixel of pinch-distance change (×sens)

inline void winSize(float &w, float &h) {
	int iw = 1640, ih = 720;
	if (TheSDL3Window) SDL_GetWindowSize(TheSDL3Window, &iw, &ih);
	w = (float)iw; h = (float)ih;
}

inline bool inLiveGame() {
	return TheTacticalView && TheGameLogic && TheGameLogic->isInGame() && !TheGameLogic->isInShellGame();
}

// A video/cutscene is on screen (either the display's own movie or any open stream).
inline bool videoPlaying() {
	return (TheVideoPlayer && TheVideoPlayer->firstStream() != NULL)
	    || (TheDisplay && TheDisplay->isMoviePlaying());
}
// The menu button is live only while playing, while the pause menu is NOT already up, and
// NOT during a cutscene (then the Skip button takes over). Matches W3DInGameUI.cpp's draw gate.
inline bool menuButtonActive() {
	return inLiveGame() && (!TheInGameUI || !TheInGameUI->isQuitMenuVisible()) && !videoPlaying();
}
inline bool onMenuButton(float px, float py) {
	return px >= MENU_BTN_X && px < MENU_BTN_X + MENU_BTN_W
	    && py >= MENU_BTN_Y && py < MENU_BTN_Y + MENU_BTN_H;
}
inline bool onSkipButton(float px, float py) {
	float w, h; winSize(w, h);
	float bx = w - SKIP_BTN_MX - SKIP_BTN_W;   // top-right, right-anchored (matches draw)
	return px >= bx && px < bx + SKIP_BTN_W && py >= SKIP_BTN_Y && py < SKIP_BTN_Y + SKIP_BTN_H;
}

// Finger-drag/zoom camera sensitivity, driven by the Options > Scroll Speed slider,
// which persists m_keyboardScrollFactor (default 0.5, slider max ≈ 1.0). Punchy mapping:
// default ≈ 3x, slider max ≈ 6x, floored at 0.5x so a drag is never dead. Both the
// 2-finger pan and the pinch-zoom use this, so one slider scales the whole camera.
inline float panSensitivity() {
	float f = TheGlobalData ? TheGlobalData->m_keyboardScrollFactor : 0.5f;
	float mult = f * 6.0f;
	if (mult < 0.5f) mult = 0.5f;
	if (mult > 8.0f) mult = 8.0f;
	return mult;
}

void synthMotion(float x, float y) {
	if (!TheMouse) return;
	SDL3Mouse *m = dynamic_cast<SDL3Mouse*>(TheMouse); if (!m) return;
	SDL_Event e; memset(&e, 0, sizeof(e));
	e.type = SDL_EVENT_MOUSE_MOTION;
	e.motion.timestamp = SDL_GetTicksNS();
	e.motion.x = x; e.motion.y = y;
	m->addSDLEvent(&e);
}
void synthButton(Uint8 button, bool down, float x, float y) {
	if (!TheMouse) return;
	SDL3Mouse *m = dynamic_cast<SDL3Mouse*>(TheMouse); if (!m) return;
	SDL_Event e; memset(&e, 0, sizeof(e));
	e.type = down ? SDL_EVENT_MOUSE_BUTTON_DOWN : SDL_EVENT_MOUSE_BUTTON_UP;
	e.button.timestamp = SDL_GetTicksNS();
	e.button.button = button;
	e.button.down = down;
	e.button.clicks = 1;
	e.button.x = x; e.button.y = y;
	m->addSDLEvent(&e);
}
// Feed a synthetic key press through the same buffer as a physical key. Used for the
// on-screen SKIP button: the movie loops poll TheKeyboard for an unused KEY_ESC down.
void synthKey(SDL_Scancode sc, bool down) {
	if (!TheKeyboard) return;
	SDL3Keyboard *kb = dynamic_cast<SDL3Keyboard*>(TheKeyboard); if (!kb) return;
	SDL_Event e; memset(&e, 0, sizeof(e));
	e.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;   // union: also sets e.key.type
	e.key.timestamp = SDL_GetTicksNS();
	e.key.scancode = sc;
	e.key.down = down;
	e.key.repeat = false;
	kb->addSDLEvent(&e);
}
// A synthesized "click" must look like a real tap: the button must stay DOWN
// for a while before the UP, or a latching GUI widget arms on the down and its
// up lands in the same client update, so the callback never fires (the button
// sticks "selected"). Empirically the shell menu needs ~150ms of press. A real
// finger tap is only ~100ms and our 45ms commit-defer eats into that, so we
// GUARANTEE a minimum press by deferring the up through this queue whenever it
// would otherwise come too soon after the down.
struct PendingUp { Uint8 button; float x, y; Uint64 dueMs; bool active; };
PendingUp    g_ups[8] = {};
Uint64       g_leftDownSynthMs = 0;    // wall-clock of the last synthesized left-down
const Uint64 CLICK_HOLD_MS = 150;      // minimum synthesized press duration

void queueUpAt(Uint8 button, float x, float y, Uint64 dueMs) {
	for (int i = 0; i < 8; ++i) if (!g_ups[i].active) { g_ups[i] = { button, x, y, dueMs, true }; return; }
	synthButton(button, false, x, y);   // queue full (unexpected): release immediately
}
void drainUps() {
	Uint64 now = SDL_GetTicks();
	for (int i = 0; i < 8; ++i) if (g_ups[i].active) {
		if (now >= g_ups[i].dueMs) {
			synthButton(g_ups[i].button, false, g_ups[i].x, g_ups[i].y);
			g_ups[i].active = false;
		} else {
			// Keep the synthesized press "alive" with a motion every frame, just
			// like a real held finger. Without this the WindowManager never sees
			// the press sustained and a pushbutton's up won't fire its callback.
			synthMotion(g_ups[i].x, g_ups[i].y);
		}
	}
}
void synthLeftDown(float x, float y) {
	synthMotion(x, y);
	synthButton(SDL_BUTTON_LEFT, true, x, y);
	g_leftHeld = true;
	g_leftDownSynthMs = SDL_GetTicks();
}
// Release a held left button, guaranteeing >= CLICK_HOLD_MS of total press so
// the GUI registers the click. Long presses/drags release immediately.
void releaseLeftHeld(float x, float y) {
	Uint64 due = g_leftDownSynthMs + CLICK_HOLD_MS;
	if (SDL_GetTicks() >= due) synthButton(SDL_BUTTON_LEFT, false, x, y);
	else                       queueUpAt(SDL_BUTTON_LEFT, x, y, due);
	g_leftHeld = false;
}
inline void rightClick(float x, float y) {
	synthMotion(x, y);
	synthButton(SDL_BUTTON_RIGHT, true, x, y);
	queueUpAt(SDL_BUTTON_RIGHT, x, y, SDL_GetTicks() + CLICK_HOLD_MS);
}

int findFinger(SDL_FingerID id) { for (int i = 0; i < g_tfCount; ++i) if (g_tf[i].id == id) return i; return -1; }

/**
 * Constructor: Initialize SDL3 game engine state
 */
SDL3GameEngine::SDL3GameEngine()
	: GameEngine(),
	  m_SDLWindow(nullptr),
	  m_IsInitialized(false),
	  m_IsActive(false),
	  m_IsTextInputActive(false),
	  m_TextInputFocusWindow(nullptr)
{
	fprintf(stderr, "DEBUG: SDL3GameEngine::SDL3GameEngine() created\n");
}

/**
 * Destructor: Cleanup SDL3 resources
 */
SDL3GameEngine::~SDL3GameEngine()
{
	if (m_SDLWindow && m_IsTextInputActive) {
		SDL_StopTextInput(m_SDLWindow);
		m_IsTextInputActive = false;
		m_TextInputFocusWindow = nullptr;
	}

	if (m_IsInitialized) {
		// Window cleanup is done in reset/shutdown
	}
	fprintf(stderr, "DEBUG: SDL3GameEngine::~SDL3GameEngine() destroyed\n");
}

/**
 * From GameEngine: init() - initialize subsystems
 * 
 * GeneralsX @bugfix felipebraz 16/02/2026
 * Simplified to follow fighter19 pattern - SDL3/Vulkan initialized in SDL3Main.cpp
 * before GameEngine is created. This init() only delegates to parent GameEngine::init().
 * ApplicationHWnd and TheSDL3Window are already set by main() before this is called.
 */
void SDL3GameEngine::init(void)
{
	fprintf(stderr, "INFO: SDL3GameEngine::init() starting\n");

	if (TheGlobalData && TheGlobalData->m_headless) {
		// GeneralsX @bugfix Copilot 17/05/2026 Allow headless replay path to initialize engine subsystems without an SDL window.
		fprintf(stderr, "INFO: SDL3GameEngine::init() headless mode - skipping SDL window binding\n");
		m_SDLWindow = nullptr;
		m_IsInitialized = true;
		m_IsActive = true;
		GameEngine::init();
		return;
	}

	// Verify window was created by SDL3Main.cpp
	extern SDL_Window* TheSDL3Window;
	extern HWND ApplicationHWnd;
	
	if (!TheSDL3Window || !ApplicationHWnd) {
		fprintf(stderr, "FATAL: SDL3 window not initialized before GameEngine::init()\n");
		fprintf(stderr, "FATAL: TheSDL3Window=%p, ApplicationHWnd=%p\n", TheSDL3Window, ApplicationHWnd);
		return;
	}

	// Store window reference locally
	m_SDLWindow = TheSDL3Window;
	m_IsInitialized = true;
	m_IsActive = true;

	fprintf(stderr, "INFO: SDL3GameEngine using pre-initialized window\n");

	// Call parent init to initialize game subsystems
	GameEngine::init();
}

/**
 * From GameEngine: reset() - reset system to starting state
 */
void SDL3GameEngine::reset(void)
{
	fprintf(stderr, "DEBUG: SDL3GameEngine::reset()\n");
	if (m_SDLWindow && m_IsTextInputActive) {
		SDL_StopTextInput(m_SDLWindow);
		m_IsTextInputActive = false;
		m_TextInputFocusWindow = nullptr;
	}
	GameEngine::reset();
}

/**
 * From GameEngine: update() - per-frame update
 */
void SDL3GameEngine::update(void)
{
	pollSDL3Events();
	GameEngine::update();
}

/**
 * From GameEngine: execute() - main game loop
 */
void SDL3GameEngine::execute(void)
{
	fprintf(stderr, "INFO: SDL3GameEngine::execute() - entering main loop\n");
	GameEngine::execute();
	fprintf(stderr, "INFO: SDL3GameEngine::execute() - exited main loop\n");
}

/**
 * From GameEngine: serviceWindowsOS() - native OS service
 * On Linux, process SDL3 events
 */
void SDL3GameEngine::serviceWindowsOS(void)
{
	pollSDL3Events();
}

/**
 * Check if game has OS focus
 */
Bool SDL3GameEngine::isActive(void)
{
	return m_IsActive;
}

/**
 * Set OS focus status
 */
void SDL3GameEngine::setIsActive(Bool isActive)
{
	m_IsActive = isActive;
}

/**
 * Poll and process SDL3 events
 * Handles keyboard, mouse, window, and quit events
 */
void SDL3GameEngine::pollSDL3Events(void)
{
	if (!m_SDLWindow) {
		return;
	}

	updateTextInputState();

	SDL_Event event;
	while (SDL_PollEvent(&event)) {
		switch (event.type) {
			case SDL_EVENT_QUIT:
				m_quitting = true;
				break;

			case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
				m_quitting = true;
				break;

			case SDL_EVENT_WINDOW_FOCUS_GAINED:
				m_IsActive = true;
				if (TheMouse) {
					TheMouse->regainFocus();
					TheMouse->refreshCursorCapture();
				}
				break;

			case SDL_EVENT_WINDOW_FOCUS_LOST:
				m_IsActive = false;
				if (m_IsTextInputActive) {
					SDL_StopTextInput(m_SDLWindow);
					m_IsTextInputActive = false;
					m_TextInputFocusWindow = nullptr;
				}
				if (TheMouse) {
					TheMouse->loseFocus();
				}
				break;

			case SDL_EVENT_WINDOW_MOUSE_ENTER:
				if (TheMouse) {
					TheMouse->onCursorMovedInside();
				}
				break;

			case SDL_EVENT_WINDOW_MOUSE_LEAVE:
				if (TheMouse) {
					TheMouse->onCursorMovedOutside();
				}
				break;

			case SDL_EVENT_KEY_DOWN:
			case SDL_EVENT_KEY_UP:
				// Fighter19 pattern: direct addSDLEvent() call
				// GeneralsX @refactor felipebraz 16/02/2026 Simplified event routing
				if (TheKeyboard) {
					SDL3Keyboard* keyboard = dynamic_cast<SDL3Keyboard*>(TheKeyboard);
					if (keyboard) {
						keyboard->addSDLEvent(&event);
					}
				}
				break;

			case SDL_EVENT_TEXT_INPUT:
				forwardTextInputEvent(event.text.text);
				break;

			case SDL_EVENT_MOUSE_MOTION:
			case SDL_EVENT_MOUSE_BUTTON_DOWN:
			case SDL_EVENT_MOUSE_BUTTON_UP:
			case SDL_EVENT_MOUSE_WHEEL:
				// Fighter19 pattern: direct addSDLEvent() call with raw SDL_Event
				// GeneralsX @refactor felipebraz 16/02/2026 Simplified event routing
				if (TheMouse) {
					SDL3Mouse* mouse = dynamic_cast<SDL3Mouse*>(TheMouse);
					if (mouse) {
						mouse->addSDLEvent(&event);
					}
				}
				break;

			case SDL_EVENT_WINDOW_RESIZED:
				handleWindowEvent(event.window);
				break;

			default:
				// Ignore other events for now
				break;
		}

		updateTextInputState();
	}
}

// GeneralsX @bugfix felipebraz 01/04/2026 Enable SDL text input only while an entry gadget owns focus.
void SDL3GameEngine::updateTextInputState(void)
{
	if (!m_SDLWindow || !TheWindowManager) {
		return;
	}

	GameWindow* focusedWindow = TheWindowManager->winGetFocus();
	const Bool wantsTextInput =
		focusedWindow != nullptr && BitIsSet(focusedWindow->winGetStyle(), GWS_ENTRY_FIELD);

	if (wantsTextInput) {
		if (!m_IsTextInputActive) {
			if (SDL_StartTextInput(m_SDLWindow)) {
				m_IsTextInputActive = true;
			}
		}
		m_TextInputFocusWindow = focusedWindow;
	} else {
		if (m_IsTextInputActive) {
			SDL_StopTextInput(m_SDLWindow);
			m_IsTextInputActive = false;
		}
		m_TextInputFocusWindow = nullptr;
	}
}

// GeneralsX @bugfix felipebraz 01/04/2026 Forward SDL UTF-8 text input through existing GWM_IME_CHAR path.
void SDL3GameEngine::forwardTextInputEvent(const char* utf8Text)
{
	if (!utf8Text || !TheWindowManager) {
		return;
	}

	// GeneralsX @bugfix felipebraz 01/04/2026 Use tracked text-input focus window to keep SDL text delivery stable.
	GameWindow* targetWindow = m_TextInputFocusWindow;
	if (!targetWindow || !BitIsSet(targetWindow->winGetStyle(), GWS_ENTRY_FIELD)) {
		return;
	}

	const size_t textLength = strlen(utf8Text);
	size_t offset = 0;
	while (offset < textLength) {
		UnsignedInt codepoint = 0;
		if (!DecodeNextUtf8Codepoint(utf8Text, textLength, offset, codepoint)) {
			continue;
		}

		// GeneralsX @bugfix felipebraz 01/04/2026 Clamp IME char forwarding to BMP and reject UTF-16 surrogate range.
		if (codepoint == 0 || codepoint > 0x10FFFFU) {
			continue;
		}

		if (codepoint >= 0xD800U && codepoint <= 0xDFFFU) {
			continue;
		}

		if (codepoint > 0xFFFFU) {
			continue;
		}

		const WideChar wideCharacter = static_cast<WideChar>(codepoint);
		TheWindowManager->winSendInputMsg(targetWindow, GWM_IME_CHAR, static_cast<WindowMsgData>(wideCharacter), 0);
	}
}

/**
 * Handle keyboard event -dispatch to Keyboard manager
 * TheSuperHackers @build 10/02/2026 BenderAI - Phase 1.5 event wiring
 */
void SDL3GameEngine::handleKeyboardEvent(const SDL_KeyboardEvent& event)
{
	// Dispatch to SDL3Keyboard if available
	if (TheKeyboard) {
		SDL3Keyboard* sdlKeyboard = dynamic_cast<SDL3Keyboard*>(TheKeyboard);
		if (sdlKeyboard) {
			sdlKeyboard->addSDL3KeyEvent(event);
		}
	}
}

/**
 * Handle mouse motion event - dispatch to Mouse manager
 * TheSuperHackers @build 10/02/2026 BenderAI - Phase 1.5 event wiring
 */
void SDL3GameEngine::handleMouseMotionEvent(const SDL_MouseMotionEvent& event)
{
	// Dispatch to SDL3Mouse if available
	if (TheMouse) {
		SDL3Mouse* sdlMouse = dynamic_cast<SDL3Mouse*>(TheMouse);
		if (sdlMouse) {
			sdlMouse->addSDL3MouseMotionEvent(event);
		}
	}
}

/**
 * Handle mouse button event - dispatch to Mouse manager
 * TheSuperHackers @build 10/02/2026 BenderAI - Phase 1.5 event wiring
 */
void SDL3GameEngine::handleMouseButtonEvent(const SDL_MouseButtonEvent& event)
{
	// Dispatch to SDL3Mouse if available
	if (TheMouse) {
		SDL3Mouse* sdlMouse = dynamic_cast<SDL3Mouse*>(TheMouse);
		if (sdlMouse) {
			sdlMouse->addSDL3MouseButtonEvent(event);
		}
	}
}

/**
 * Handle mouse wheel event - dispatch to Mouse manager
 * TheSuperHackers @build 10/02/2026 BenderAI - Phase 1.5 event wiring
 */
void SDL3GameEngine::handleMouseWheelEvent(const SDL_MouseWheelEvent& event)
{
	// Dispatch to SDL3Mouse if available
	if (TheMouse) {
		SDL3Mouse* sdlMouse = dynamic_cast<SDL3Mouse*>(TheMouse);
		if (sdlMouse) {
			sdlMouse->addSDL3MouseWheelEvent(event);
		}
	}
}

/**
 * Handle window event (resize, etc.)
 */
void SDL3GameEngine::handleWindowEvent(const SDL_WindowEvent& event)
{
	// TODO: Phase 2 - Handle window resize, notify graphics subsystem
	// fprintf(stderr, "DEBUG: Window event (type=%d)\n", event.type);
}

/**
 * Factory Methods for GameEngine subsystems
 * TheSuperHackers @build felipebraz 13/02/2026
 * Implementations in .cpp to provide complete type definitions and avoid circular includes
 */

LocalFileSystem *SDL3GameEngine::createLocalFileSystem(void)
{
	fprintf(stderr, "INFO: SDL3GameEngine::createLocalFileSystem() -> StdLocalFileSystem\n");
	return NEW StdLocalFileSystem;
}

ArchiveFileSystem *SDL3GameEngine::createArchiveFileSystem(void)
{
	fprintf(stderr, "INFO: SDL3GameEngine::createArchiveFileSystem() -> StdBIGFileSystem\n");
	return NEW StdBIGFileSystem;
}

GameLogic *SDL3GameEngine::createGameLogic(void)
{
	fprintf(stderr, "INFO: SDL3GameEngine::createGameLogic() -> W3DGameLogic\n");
	return NEW W3DGameLogic;
}

GameClient *SDL3GameEngine::createGameClient(void)
{
	fprintf(stderr, "INFO: SDL3GameEngine::createGameClient() -> W3DGameClient\n");
	return NEW W3DGameClient;
}

ModuleFactory *SDL3GameEngine::createModuleFactory(void)
{
	fprintf(stderr, "INFO: SDL3GameEngine::createModuleFactory() -> W3DModuleFactory\n");
	return NEW W3DModuleFactory;
}

ThingFactory *SDL3GameEngine::createThingFactory(void)
{
	fprintf(stderr, "INFO: SDL3GameEngine::createThingFactory() -> W3DThingFactory\n");
	return NEW W3DThingFactory;
}

FunctionLexicon *SDL3GameEngine::createFunctionLexicon(void)
{
	fprintf(stderr, "INFO: SDL3GameEngine::createFunctionLexicon() -> W3DFunctionLexicon\n");
	return NEW W3DFunctionLexicon;
}

// GeneralsX @bugfix Copilot 15/04/2026 Match upstream GameEngine pure-virtual signature after sync.
Radar *SDL3GameEngine::createRadar(Bool dummy)
{
	// GeneralsX @bugfix fbraz 04/05/2026 Respect headless mode and create dummy radar.
	// Upstream reference: Win32GameEngine headless factory behavior, TheSuperHackers/GeneralsGameCode
	// https://github.com/TheSuperHackers/GeneralsGameCode
	if (dummy) {
		fprintf(stderr, "INFO: SDL3GameEngine::createRadar() -> RadarDummy (headless)\n");
		return NEW RadarDummy;
	}
	fprintf(stderr, "INFO: SDL3GameEngine::createRadar() -> W3DRadar\n");
	return NEW W3DRadar;
}

// GeneralsX @bugfix Copilot 24/03/2026 Match upstream GameEngine pure-virtual signature after sync.
ParticleSystemManager* SDL3GameEngine::createParticleSystemManager(Bool dummy)
{
	// GeneralsX @bugfix fbraz 04/05/2026 Respect headless mode and create dummy particle manager.
	if (dummy) {
		fprintf(stderr, "INFO: SDL3GameEngine::createParticleSystemManager() -> ParticleSystemManagerDummy (headless)\n");
		return NEW ParticleSystemManagerDummy;
	}
	fprintf(stderr, "INFO: SDL3GameEngine::createParticleSystemManager() -> W3DParticleSystemManager\n");
	return NEW W3DParticleSystemManager;
}

WebBrowser *SDL3GameEngine::createWebBrowser(void)
{
	// WebBrowser uses Windows COM (CComObject<W3DWebBrowser>)
	// Not available on Linux - return nullptr
	fprintf(stderr, "WARNING: WebBrowser not available on Linux platform\n");
	return nullptr;
}

/**
 * Factory method: AudioManager
 * Select audio backend based on compile flags
 * GeneralsX @bugfix Copilot 15/04/2026 Match upstream GameEngine pure-virtual signature after sync.
 */
AudioManager *SDL3GameEngine::createAudioManager(Bool dummy)
{
	(void)dummy;
	fprintf(stderr, "INFO: SDL3GameEngine::createAudioManager()\n");

#ifdef SAGE_USE_MINIAUDIO
	fprintf(stderr, "INFO: Creating MiniAudio audio backend\n");
	return new MiniAudioManager();
#elif defined(SAGE_USE_OPENAL)
	fprintf(stderr, "INFO: Creating OpenAL audio backend\n");
	return new OpenALAudioManager();
#else
	fprintf(stderr, "INFO: Audio backend not available (SAGE_USE_OPENAL/SAGE_USE_MINIAUDIO not defined)\n");
	fprintf(stderr, "WARNING: Falls back to parent implementation or silent mode\n");
	return GameEngine::createAudioManager();  // Call parent (may return stub)
#endif
}

#endif // !_WIN32

