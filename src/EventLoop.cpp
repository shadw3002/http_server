#include "EventLoop.h"
#include "CurrentThread.h"
#include <assert.h>
#include <sys/eventfd.h>

auto less = [](auto a, auto b){
  if (a->next_active().tv_sec < b->next_active().tv_sec)
    return true;
  if (a->next_active().tv_sec == b->next_active().tv_sec)
    return a->next_active().tv_usec < b->next_active().tv_usec;

  return false;
};

static int create_eventfd()
{
  int evfd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
  if (evfd < 0) {
    // TODO
  }
  return evfd;
}

static void wakeup_read_handle(Channel* channel)
{
  uint64_t one = 1;
  ssize_t n = ::read(channel->fd(), &one, sizeof one);
  if (n != sizeof one) {
    // TODO
  }
}

EventLoop::EventLoop()
  : m_looping(false)
  , m_thread_id(CurrentThread::tid())
  , m_wakeup_channel(this, create_eventfd())
{
  using namespace std::placeholders;

  m_timerheap.set_comp(less);

  m_wakeup_channel.set_read_callback(std::bind(wakeup_read_handle, _1));
  m_wakeup_channel.enable_read();
}

EventLoop::~EventLoop()
{
  close(m_wakeup_channel.fd());
}

#include <iostream>

void EventLoop::loop()
{
  m_looping = true;

  while (!m_looping) {
    int timeout = -1;
    if (!m_timerheap.empty()) {
      struct timeval now; gettimeofday(&now, NULL);
      const struct timeval& min_time = m_timerheap.top()->next_active();
      timeout = (min_time.tv_sec - now.tv_sec) * 1000
        + ((min_time.tv_usec - now.tv_usec) / 1000)
        + 100;

      if (timeout < 0) timeout = 0;
    }

    m_active_channels.clear();
    m_epoller.poll(m_active_channels, timeout);

    process_active_events();
    do_pending_functors();
  }
}

void EventLoop::process_active_time_events()
{
  struct timeval now; gettimeofday(&now, NULL);

  while (!m_timerheap.empty() && less(m_timerheap.top()->next_active(), now)) {
    auto timer = *m_timerheap.top(); m_timerheap.pop();
    timer->do_callback();
    if (timer->is_repeat()) m_timerheap.push(timer);
    else delete timer;
  }
}

void EventLoop::process_active_events()
{
  for (auto channel : m_active_channels) {
    channel->handle_event();
  }
}

void EventLoop::quit()
{
  m_looping = true;
}

bool EventLoop::is_in_loop_thread() const
{
  return m_thread_id == CurrentThread::tid();
}

void EventLoop::assert_in_loop_thread()
{
  if (!is_in_loop_thread()) assert(false);
}

void EventLoop::add_channel(Channel* channel)
{
  if (has_channel(channel)) {
    // TODO
  } else {
    m_epoller.add_channel(channel);
    m_fd2channel_map.insert({channel->fd(), channel});
  }
}

void EventLoop::update_channel(Channel* channel)
{
  if (has_channel(channel)) {
    m_epoller.mod_channel(channel);
  } else {
    // TODO
  }
}

bool EventLoop::has_channel(Channel* channel)
{
  return m_fd2channel_map.find(channel->fd()) != m_fd2channel_map.end();
}

void EventLoop::do_functor(const Functor& functor)
{
  if (is_in_loop_thread()) functor();
  else queue_functor(functor);
}

void EventLoop::queue_functor(const Functor& functor)
{
  {
    std::lock_guard lk(m_mtx);
    m_pending_functors.push_back(functor);
  }

  wake_up();
}

void EventLoop::wake_up()
{
  uint64_t one = 1;
  ssize_t n = ::write(m_wakeup_channel.fd(), &one, sizeof one);
  if (n != sizeof one) {
    // TODO
  }
}

void EventLoop::do_pending_functors()
{
  std::vector<Functor> functors;

  {
    std::lock_guard lock(m_mtx);
    functors.swap(m_pending_functors);
  }

  for (auto f : functors) f();
}

void EventLoop::remove_channel(Channel* channel)
{
  int fd = channel->fd();
  m_fd2channel_map.erase(fd);
  m_epoller.del_channel(channel);

  delete channel;
}

void EventLoop::add_timer(Timer* timer)
{
  m_timerheap.push(timer);
}