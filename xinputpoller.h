// (C) Rico Possienka
// License MIT

#include <Xinput.h>
#include <Windows.h>
#include "thread.h" //https://github.com/mattiasgustavsson/libs/blob/main/thread.h

struct xevent {
  XINPUT_STATE xstate;
  int player;
};

enum { XINPUT_POOL_SIZE = 16 * XUSER_MAX_COUNT };

enum PollerState {
  POLLER_STATE_DEAD = 0,
  POLLER_STATE_IDLE,
  POLLER_STATE_BLOCKED,  // Poller Thread is writing XInput state into array right now.
  POLLER_STATE_COPY,     // Main Thread is reading from array right now, blocking Xinput.
};

struct XInputPoller {
  thread_ptr_t thread;
  thread_atomic_int_t state;
  // thread_mutex_t mutex;

  // state accessed in different threads
  xevent xstates[XINPUT_POOL_SIZE];
  size_t xstate_length = 0;

  XInputPoller() {
    // thread_mutex_init(&mutex);
    thread_atomic_int_store(&state, POLLER_STATE_IDLE);
  }

  static int thread_proc(void *user) {
    XInputPoller *that = reinterpret_cast<XInputPoller *>(user);
    DWORD last_pkt[XUSER_MAX_COUNT] = {~0, ~0, ~0, ~0};
    XINPUT_CAPABILITIES capabilities;
    thread_timer_t timer;
    thread_timer_init(&timer);

    const unsigned int max_read_count = 512;
    unsigned int read_count = 0;
    bool connected[XUSER_MAX_COUNT] = {false};
    int player_number[XUSER_MAX_COUNT] = {0};

    for (;;) {
      int old_state = thread_atomic_int_compare_and_swap(&that->state, POLLER_STATE_IDLE, POLLER_STATE_BLOCKED);  // LOCK
      if (old_state == POLLER_STATE_DEAD) break;
      if (old_state != POLLER_STATE_IDLE) continue;

      if (read_count % max_read_count == 0) {
        // TODO: XInputGetXXXX hangs when controller is getting disconnected.
        //       Call GetCapabilities only once every second or so
        //       https://stackoverflow.com/a/51943338

        int player = 0;
        for (int user_id = 0; user_id != XUSER_MAX_COUNT; ++user_id) {
          bool is_connected = XInputGetCapabilities(user_id, XINPUT_FLAG_GAMEPAD, &capabilities) == ERROR_SUCCESS;
          connected[user_id] = is_connected;
          if (is_connected) {
            player_number[user_id] = player++;
          }
        }
      }
      read_count = (read_count + 1) % max_read_count;

      bool new_packet = true;
      while (new_packet) {
        new_packet = false;
        for (DWORD userIndex = 0; userIndex != XUSER_MAX_COUNT; ++userIndex) {
          if (!connected[userIndex]) continue;
          if (that->xstate_length == XINPUT_POOL_SIZE) continue;
          xevent &x = that->xstates[that->xstate_length];
          if (XInputGetState(userIndex, &x.xstate) != ERROR_SUCCESS) {
            connected[userIndex] = false;
            for (int user_id = userIndex; user_id != XUSER_MAX_COUNT; ++user_id) {
              --player_number[user_id];
            }
            continue;
          }
          if (x.xstate.dwPacketNumber == last_pkt[userIndex]) continue;
          new_packet = true;
          x.player = player_number[userIndex];
          ++that->xstate_length;
          last_pkt[userIndex] = x.xstate.dwPacketNumber;
        }
      }

      old_state = thread_atomic_int_compare_and_swap(&that->state, POLLER_STATE_BLOCKED, POLLER_STATE_IDLE);  // UNLOCK
      assert(old_state == POLLER_STATE_BLOCKED);
      thread_timer_wait(&timer, 1 * 1000 * 1000);  // sleep for a ms
    }
    thread_timer_term(&timer);
    return 0;
  }

  size_t load_xstate(xevent *array, size_t max) {
    int old_state = thread_atomic_int_compare_and_swap(&state, POLLER_STATE_IDLE, POLLER_STATE_COPY);
    assert(old_state != POLLER_STATE_COPY);
    if (old_state != POLLER_STATE_IDLE) return 0;

    size_t i = 0;
    for (;; ++i) {
      if (i == xstate_length) break;
      if (i == max) break;
      array[i] = xstates[i];
    }
    xstate_length = 0;
    old_state = thread_atomic_int_compare_and_swap(&state, POLLER_STATE_COPY, POLLER_STATE_IDLE);
    assert(old_state == POLLER_STATE_COPY);
    return i;
  }

  template <size_t N>
  size_t load_xstate(xevent (&array)[N]) {
    return load_xstate(array, N);
  }

  void start_thread() { thread = thread_create(thread_proc, this, "XInput Poller", THREAD_STACK_SIZE_DEFAULT); }
  void stop_thread() {
    while (thread_atomic_int_compare_and_swap(&state, POLLER_STATE_IDLE, POLLER_STATE_DEAD) == POLLER_STATE_IDLE) {
      // Loop until dead.
    }
  }
} g_xinputpoller;
