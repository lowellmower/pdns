#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include "mplexer.hh"
#include "sstuff.hh"
#include <iostream>
#include <poll.h>
#include "misc.hh"
#include "namespaces.hh"

FDMultiplexer* FDMultiplexer::getMultiplexerSilent()
{
  FDMultiplexer* ret = nullptr;
  for(const auto& i : FDMultiplexer::getMultiplexerMap()) {
    try {
      ret = i.second();
      return ret;
    }
    catch(const FDMultiplexerException& fe) {
    }
    catch(...) {
    }
  }
  return ret;
}


class PollFDMultiplexer : public FDMultiplexer
{
public:
  PollFDMultiplexer()
  {}
  virtual ~PollFDMultiplexer()
  {
  }

  virtual int run(struct timeval* tv, int timeout=500);

  virtual void addFD(callbackmap_t& cbmap, int fd, callbackfunc_t toDo, const funcparam_t& parameter);
  virtual void removeFD(callbackmap_t& cbmap, int fd);
  string getName()
  {
    return "poll";
  }
private:
};

static FDMultiplexer* make()
{
  return new PollFDMultiplexer();
}

static struct RegisterOurselves
{
  RegisterOurselves() {
    FDMultiplexer::getMultiplexerMap().insert(make_pair(1, &make));
  }
} doIt;

void PollFDMultiplexer::addFD(callbackmap_t& cbmap, int fd, callbackfunc_t toDo, const boost::any& parameter)
{
  Callback cb;
  cb.d_callback=toDo;
  cb.d_parameter=parameter;
  memset(&cb.d_ttd, 0, sizeof(cb.d_ttd));
  if(cbmap.count(fd))
    throw FDMultiplexerException("Tried to add fd "+std::to_string(fd)+ " to multiplexer twice");
  cbmap[fd]=cb;
}

void PollFDMultiplexer::removeFD(callbackmap_t& cbmap, int fd)
{
  if(d_inrun && d_iter->first==fd)  // trying to remove us!
    d_iter++;

  if(!cbmap.erase(fd))
    throw FDMultiplexerException("Tried to remove unlisted fd "+std::to_string(fd)+ " from multiplexer");
}

bool pollfdcomp(const struct pollfd& a, const struct pollfd& b)
{
  return a.fd < b.fd;
}

int PollFDMultiplexer::run(struct timeval* now, int timeout)
{
  if(d_inrun) {
    throw FDMultiplexerException("FDMultiplexer::run() is not reentrant!\n");
  }
  
  vector<struct pollfd> pollfds;
  
  struct pollfd pollfd;
  for(callbackmap_t::const_iterator i=d_readCallbacks.begin(); i != d_readCallbacks.end(); ++i) {
    pollfd.fd = i->first;
    pollfd.events = POLLIN;
    pollfds.push_back(pollfd);
  }

  for(callbackmap_t::const_iterator i=d_writeCallbacks.begin(); i != d_writeCallbacks.end(); ++i) {
    pollfd.fd = i->first;
    pollfd.events = POLLOUT;
    pollfds.push_back(pollfd);
  }

  int ret=poll(&pollfds[0], pollfds.size(), timeout);
  gettimeofday(now, 0); // MANDATORY!
  
  if(ret < 0 && errno!=EINTR)
    throw FDMultiplexerException("poll returned error: "+stringerror());

  d_iter=d_readCallbacks.end();
  d_inrun=true;

  for(unsigned int n = 0; n < pollfds.size(); ++n) {  
    if(pollfds[n].revents == POLLIN) {
      d_iter=d_readCallbacks.find(pollfds[n].fd);
    
      if(d_iter != d_readCallbacks.end()) {
        d_iter->second.d_callback(d_iter->first, d_iter->second.d_parameter);
        continue; // so we don't refind ourselves as writable!
      }
    }
    else if(pollfds[n].revents == POLLOUT) {
      d_iter=d_writeCallbacks.find(pollfds[n].fd);
    
      if(d_iter != d_writeCallbacks.end()) {
        d_iter->second.d_callback(d_iter->first, d_iter->second.d_parameter);
      }
    }
  }
  d_inrun=false;
  return ret;
}

#if 0

void acceptData(int fd, boost::any& parameter)
{
  cout<<"Have data on fd "<<fd<<endl;
  Socket* sock=boost::any_cast<Socket*>(parameter);
  string packet;
  IPEndpoint rem;
  sock->recvFrom(packet, rem);
  cout<<"Received "<<packet.size()<<" bytes!\n";
}


int main()
{
  Socket s(AF_INET, SOCK_DGRAM);
  
  IPEndpoint loc("0.0.0.0", 2000);
  s.bind(loc);

  PollFDMultiplexer sfm;

  sfm.addReadFD(s.getHandle(), &acceptData, &s);

  for(int n=0; n < 100 ; ++n) {
    sfm.run();
  }
  sfm.removeReadFD(s.getHandle());
  sfm.removeReadFD(s.getHandle());
}
#endif

