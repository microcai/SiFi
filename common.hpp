
#pragma once

#include <boost/math/constants/constants.hpp>

template <class T>
T circumference(T r)
{
   return boost::math::constants::two_pi<T>() * r;
}
