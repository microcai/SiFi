
#pragma once

#include <deque>

#include <mutex>
#include <condition_variable>
#include <boost/noncopyable.hpp>

template<int size, typename _Tp, typename _Alloc = std::allocator<_Tp> >
class dqueue : boost::noncopyable
{
public:
	typedef _Tp value_type;
public:
	// 拉数据，要是木有的话就睡觉去
	value_type pull()
	{
		std::unique_lock<std::mutex> l(_m);
		
		while( _q.empty())
		{
			_c_pull.wait(l);
		}
		
		value_type v = _q.front();
		
		_q.pop_front();
		_c_push.notify_one();
		
		return v;
	}
	
	void push(const value_type & v)
	{
		std::unique_lock<std::mutex> l(_m);
		
		while( _q.size() >= size )
		{
			// 等待数据被拉走
			_c_push.wait(l);
		}
		
		_q.push_back(v);

		_c_pull.notify_one();
	}
	
	bool empty()
	{
		std::unique_lock<std::mutex> l(_m);
		
		return _q.empty();
	}

private:
	std::deque<_Tp, _Alloc> _q;
	std::condition_variable _c_push;
	std::condition_variable _c_pull;
	std::mutex _m;
};

