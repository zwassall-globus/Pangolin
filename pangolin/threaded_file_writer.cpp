/* This file is part of the Pangolin Project.
 * http://github.com/stevenlovegrove/Pangolin
 *
 * Copyright (c) 2011 Steven Lovegrove
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use,
 * copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following
 * conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
 * OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
 * HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

#include "threaded_file_writer.h"

using namespace std;

namespace pangolin
{

ThreadedFileWriter::ThreadedFileWriter( const std::string& filename, unsigned int buffer_size_bytes )
    : mem_buffer(0), mem_size(0), mem_start(0), mem_end(0)
{
    file.open(filename.c_str(), ios::binary );

    mem_max_size = buffer_size_bytes;
    mem_buffer = new char[mem_max_size];

    write_thread = boost::thread(boost::ref(*this));
}

ThreadedFileWriter::~ThreadedFileWriter()
{
    if( write_thread.joinable() )
    {
      write_thread.interrupt();
      write_thread.join();
    }

    if( mem_buffer) delete mem_buffer;
    file.close();
}

int ThreadedFileWriter::write(char* data, int num_bytes)
{
    if( num_bytes > mem_max_size )
        throw exception();

    update_mutex.lock();

    // wait until there is space to write into buffer
    while( mem_size + num_bytes > mem_max_size ) {
        cond_dequeued.wait(update_mutex);
    }

    // add image to end of mem_buffer
    const int array_a_size =
        (mem_start <= mem_end) ? (mem_max_size - mem_end) : (mem_start - mem_end);

    if( num_bytes <= array_a_size )
    {
        // copy in one
        memcpy(mem_buffer + mem_end, data, num_bytes);
        mem_end += num_bytes;
        mem_size += num_bytes;
    }else{
        const int array_b_size = num_bytes - array_a_size;
        memcpy(mem_buffer + mem_end, data, array_a_size);
        memcpy(mem_buffer, data+array_a_size, array_b_size);
        mem_end = array_b_size;
        mem_size += num_bytes;
    }

    if(mem_end == mem_max_size)
        mem_end = 0;

    update_mutex.unlock();

    cond_queued.notify_one();

    return num_bytes;
}

void ThreadedFileWriter::operator()()
{
    int data_to_write = 0;

    while(true)
    {
        update_mutex.lock();

        while( mem_size == 0 )
            cond_queued.wait(update_mutex);

        data_to_write =
                (mem_start < mem_end) ?
                    mem_end - mem_start :
                    mem_max_size - mem_start;

        update_mutex.unlock();

        file.write(mem_buffer + mem_start, data_to_write );

        update_mutex.lock();

        mem_size -= data_to_write;
        mem_start += data_to_write;

        if(mem_start == mem_max_size)
            mem_start = 0;

        update_mutex.unlock();

        cond_dequeued.notify_all();
    }
}

}