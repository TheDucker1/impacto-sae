#pragma once
#include <SDL3/SDL.h>

namespace Impacto::Saekano {
// One platform ingress for the user's Vita keyboard layout. Existing owners
// consume canonical action keys, never the physical keyboard's spelling.
// Internal capture events have no scancode and already contain action keys.
inline void TranslatePcControls(SDL_Event& event) {
  if(event.type==SDL_EVENT_KEY_DOWN || event.type==SDL_EVENT_KEY_UP) {
    if(event.key.scancode==SDL_SCANCODE_UNKNOWN)return;
    switch(event.key.key) {
      case SDLK_Z:event.key.key=SDLK_RETURN;break; // Circle.
      case SDLK_X:event.key.key=SDLK_BACKSPACE;break; // Cross.
      case SDLK_A:event.key.key=SDLK_SPACE;break; // Square.
      case SDLK_S:event.key.key=SDLK_1;break; // Triangle.
      case SDLK_RETURN:event.key.key=SDLK_F13;break; // Start.
      case SDLK_TAB:event.key.key=SDLK_A;break; // Select/auto.
      case SDLK_Q:case SDLK_E:case SDLK_UP:case SDLK_DOWN:case SDLK_LEFT:case SDLK_RIGHT:
      case SDLK_LCTRL:case SDLK_RCTRL:break;
      default:event.key.key=SDLK_UNKNOWN;break;
    }
  } else if(event.type==SDL_EVENT_GAMEPAD_BUTTON_DOWN || event.type==SDL_EVENT_GAMEPAD_BUTTON_UP) {
    SDL_Keycode key=SDLK_UNKNOWN;
    switch(event.gbutton.button) {
      case SDL_GAMEPAD_BUTTON_EAST:key=SDLK_RETURN;break;
      case SDL_GAMEPAD_BUTTON_SOUTH:key=SDLK_BACKSPACE;break;
      case SDL_GAMEPAD_BUTTON_WEST:key=SDLK_SPACE;break;
      case SDL_GAMEPAD_BUTTON_NORTH:key=SDLK_1;break;
      case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER:key=SDLK_Q;break;
      case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER:key=SDLK_E;break;
      case SDL_GAMEPAD_BUTTON_START:key=SDLK_F13;break;
      case SDL_GAMEPAD_BUTTON_BACK:key=SDLK_A;break;
      case SDL_GAMEPAD_BUTTON_DPAD_UP:key=SDLK_UP;break;
      case SDL_GAMEPAD_BUTTON_DPAD_DOWN:key=SDLK_DOWN;break;
      case SDL_GAMEPAD_BUTTON_DPAD_LEFT:key=SDLK_LEFT;break;
      case SDL_GAMEPAD_BUTTON_DPAD_RIGHT:key=SDLK_RIGHT;break;
      default:break;
    }
    const bool down=event.type==SDL_EVENT_GAMEPAD_BUTTON_DOWN;
    const auto timestamp=event.gbutton.timestamp;
    event={};event.type=down?SDL_EVENT_KEY_DOWN:SDL_EVENT_KEY_UP;
    event.key.timestamp=timestamp;event.key.key=key;event.key.down=down;
  }
}
}
