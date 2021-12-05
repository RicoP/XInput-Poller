#include <Xinput.h>
#include "thread.h" //https://github.com/mattiasgustavsson/libs/blob/main/thread.h

struct XInputPoller {
  thread_ptr_t thread;
  thread_mutex_t mutex;
  thread_atomic_int_t running;

  // state accessed in different threads
  XINPUT_STATE xstates[1024];
  size_t xstate_length = 0;

  XInputPoller() {
    thread_mutex_init(&mutex);
    thread_atomic_int_store(&running, 1);
  }

  static int thread_proc(void *user) {
    XInputPoller *that = reinterpret_cast<XInputPoller *>(user);
    DWORD last_pkt = ~0;
    thread_timer_t timer;
    thread_timer_init(&timer);

    while (thread_atomic_int_load(&that->running)) {
      thread_mutex_lock(&that->mutex);
      {
        if (that->xstate_length != 1024) {
          XINPUT_STATE &x = that->xstates[that->xstate_length];
          for(;;) {
            if (XInputGetState(0, &x) != ERROR_SUCCESS) break;
            if (x.dwPacketNumber == last_pkt) break;
            last_pkt = x.dwPacketNumber;
            ++that->xstate_length;
            if (that->xstate_length == 1024) break;
          }
        }
      }
      thread_mutex_unlock(&that->mutex);
      thread_timer_wait(&timer, 1000 * 1000);  // sleep for a ms
    }
    thread_timer_term(&timer);
    return 0;
  }

  size_t load_xstate(XINPUT_STATE *array, size_t max) {
    size_t i = 0;
    thread_mutex_lock(&mutex);
    {
      for (; i != max; ++i) {
        if (i == xstate_length) break;
        array[i] = xstates[i];
      }
      xstate_length = 0;
    }
    thread_mutex_unlock(&mutex);
    return i;
  }

  void start_thread() { thread = thread_create(thread_proc, this, "XInput Poller", THREAD_STACK_SIZE_DEFAULT); }
  void stop_thread() { thread_atomic_int_store(&running, 0); }

} g_xinputpoller;
