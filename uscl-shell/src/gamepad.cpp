#include "gamepad.h"

#include <SDL3/SDL.h>

#include <cstdlib>

namespace
{
	constexpr int repeat_delay_ms = 400, repeat_every_ms = 90, home_hold_ms = 1200;
	constexpr int stick_on = 24000, stick_off = 12000; // hysteresis, out of 32767
}

Gamepad::Gamepad(QObject* parent)
	: QObject(parent)
{
	SDL_SetHint(SDL_HINT_NO_SIGNAL_HANDLERS, "1"); // SDL would swallow SIGTERM/SIGINT
	SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1"); // the game's window has focus while it runs
	m_ok = SDL_Init(SDL_INIT_GAMEPAD);
	if (!m_ok)
	{
		qWarning("SDL gamepad init failed: %s", SDL_GetError());
		return;
	}
	refreshPads();
	connect(&m_timer, &QTimer::timeout, this, &Gamepad::poll);
	m_timer.start(8);
}

Gamepad::~Gamepad()
{
	if (m_ok)
		SDL_Quit();
}

void Gamepad::refreshPads()
{
	int n = 0;
	SDL_JoystickID* ids = SDL_GetGamepads(&n);
	m_name.clear();
	for (int i = 0; i < n; i++)
	{
		if (!SDL_GetGamepadFromID(ids[i]))
			SDL_OpenGamepad(ids[i]);
		if (m_name.isEmpty())
			m_name = QString::fromUtf8(SDL_GetGamepadNameForID(ids[i]));
	}
	SDL_free(ids);
	m_count = n;
	emit countChanged();
}

void Gamepad::direction(const QString& dir, bool down)
{
	if (down)
	{
		m_held_dir = dir;
		m_held_since.start();
		m_next_repeat = repeat_delay_ms;
		emit pressed(dir);
	}
	else if (m_held_dir == dir)
	{
		m_held_dir.clear();
	}
}

void Gamepad::poll()
{
	SDL_Event e;
	while (SDL_PollEvent(&e))
	{
		switch (e.type)
		{
		case SDL_EVENT_GAMEPAD_ADDED:
		case SDL_EVENT_GAMEPAD_REMOVED:
			refreshPads();
			break;
		case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
		case SDL_EVENT_GAMEPAD_BUTTON_UP:
		{
			const bool down = e.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN;
			switch (e.gbutton.button)
			{
			case SDL_GAMEPAD_BUTTON_DPAD_UP: direction("up", down); break;
			case SDL_GAMEPAD_BUTTON_DPAD_DOWN: direction("down", down); break;
			case SDL_GAMEPAD_BUTTON_DPAD_LEFT: direction("left", down); break;
			case SDL_GAMEPAD_BUTTON_DPAD_RIGHT: direction("right", down); break;
			case SDL_GAMEPAD_BUTTON_SOUTH: if (down) emit pressed("accept"); break;
			case SDL_GAMEPAD_BUTTON_EAST: if (down) emit pressed("back"); break;
			case SDL_GAMEPAD_BUTTON_NORTH: if (down) emit pressed("options"); break;
			case SDL_GAMEPAD_BUTTON_WEST: if (down) emit pressed("info"); break;
			case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER: if (down) emit pressed("l1"); break;
			case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER: if (down) emit pressed("r1"); break;
			case SDL_GAMEPAD_BUTTON_START: if (down) emit pressed("options"); break;
			case SDL_GAMEPAD_BUTTON_GUIDE:
				if (down)
				{
					m_home_since.start();
					m_home_fired = false;
					emit pressed("home");
				}
				else
				{
					m_home_since.invalidate();
				}
				break;
			default: break;
			}
			break;
		}
		case SDL_EVENT_GAMEPAD_AXIS_MOTION:
		{
			const int v = e.gaxis.value;
			const auto step = [&](int& state, const char* neg, const char* pos) {
				const int next = v > stick_on ? 1 : v < -stick_on ? -1 : (std::abs(v) < stick_off ? 0 : state);
				if (next == state)
					return;
				if (state)
					direction(state < 0 ? neg : pos, false);
				state = next;
				if (state)
					direction(state < 0 ? neg : pos, true);
			};
			if (e.gaxis.axis == SDL_GAMEPAD_AXIS_LEFTX)
				step(m_stick_x, "left", "right");
			else if (e.gaxis.axis == SDL_GAMEPAD_AXIS_LEFTY)
				step(m_stick_y, "up", "down");
			break;
		}
		default: break;
		}
	}

	if (!m_held_dir.isEmpty() && m_held_since.elapsed() >= m_next_repeat)
	{
		m_next_repeat += repeat_every_ms;
		emit pressed(m_held_dir);
	}
	if (m_home_since.isValid() && !m_home_fired && m_home_since.elapsed() >= home_hold_ms)
	{
		m_home_fired = true;
		emit homeHeld();
	}
}
