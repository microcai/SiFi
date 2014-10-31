
#pragma once

#include <boost/asio/buffer.hpp>
#include <pulse/simple.h>


class pa_simple
{
public:
    pa_simple(
        const char *server,                 /**< Server name, or NULL for default */
        const char *name,                   /**< A descriptive name for this client (application name, ...) */
        ::pa_stream_direction_t dir,          /**< Open this stream for recording or playback? */
        const char *dev,                    /**< Sink (resp. source) name, or NULL for default */
        const char *stream_name,            /**< A descriptive name for this stream (application name, song title, ...) */
        const ::pa_sample_spec *ss,           /**< The sample type to use */
        const ::pa_channel_map *map,          /**< The channel map to use, or NULL for default */
        const ::pa_buffer_attr *attr         /**< Buffering attributes, or NULL for default */
    ) {
        int error = 0;
        _pa_simple = ::pa_simple_new(server, name, dir, dev, stream_name, ss, map, attr, & error);
    }

    template<typename MutableBuffer>
    int read(const MutableBuffer & buf)
    {
        int r = 0, error = 0;
        ( r = pa_simple_read(_pa_simple, boost::asio::buffer_cast<void *>(buf),
                             boost::asio::buffer_size(buf), &error) );
        {
            usleep(10);
        }
        return r;
    }

    template<typename ConstBuffer>
    int write(const ConstBuffer & buf)
    {
        int error = 0;

        return  pa_simple_write(_pa_simple, boost::asio::buffer_cast<void *>(buf),
                                boost::asio::buffer_size(buf), &error) ;
        {
            usleep(10);
        }
    }

    ~pa_simple()
    {
        int error = 0;
        ::pa_simple_drain(_pa_simple, &error);
        ::pa_simple_free(_pa_simple);
    }
    pa_simple * get() {
        return _pa_simple;
    }
private:
    ::pa_simple * _pa_simple;
};
