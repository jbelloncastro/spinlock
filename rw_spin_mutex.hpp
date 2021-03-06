//
// MIT License
//
// Copyright (c) 2016 Jorge Bellon Castro
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.
//

#ifndef RW_SPIN_MUTEX_HPP
#define RW_SPIN_MUTEX_HPP

// Problem with readwrite locks is that any lock has to be performed in a
// single atomic operation (otherwirse the complete locking procedure is not
// atomic, since is composed by several independent operations).
// Therefore, all the information has to be packed into a single element.

#include <cstdint>

class rw_spin_mutex {
   union rw_fields {
      struct {
         bool     _writer_waiting:1;
         bool     _writer_present:1;
         uint32_t _readers_present:30;
      };
      uint32_t    _value;
   };

public:
   rw_spin_mutex() {
      __atomic_store_n( &_fields._value, 0U,  __ATOMIC_RELAXED );
   }

   // This class is non-copyable
   rw_spin_mutex( const rw_spin_mutex& ) = delete;

   void read_unlock() {
      // Decrement _readers_present counter value
      __atomic_fetch_sub(
            &_fields._value, /* use value instead of the union     */
            1U<<2,           /* decrement _readers_present counter */
            __ATOMIC_RELEASE ); /* memorder                        */
   }

   void read_lock() {
     // Wait until no writers are present or waiting
     // If none, increase _readers_present counter
     
     rw_fields current, updated;
     current._value = __atomic_load_n( &_fields._value, __ATOMIC_RELAXED );
     
     bool success = false;
     while(!success) {
       // We expect that no writers are neither present
       // nor waiting. If there are, the CAS atomic operation
       // will fail.
       current._writer_waiting = false;
       current._writer_present = false;
       updated = current;
       updated._readers_present++;
       
       success = __atomic_compare_exchange_n (
         &_fields._value,/* destination    */
         &current._value,/* expected value */
         updated._value, /* desired value  */
         true/*weak version, more efficient than strong if in loop*/,
         __ATOMIC_ACQUIRE, /*success memorder*/
         __ATOMIC_RELAXED);/*failure memorder*/
     }
   }

   void speculative_read_lock() {
      // Speculatively assumes that no writers are present or waiting.
      // Increases the number of present readers. Afterwards, if writers are
      // detected, rollback and wait until they are finished.
      rw_fields current;
      current._value = __atomic_fetch_add(
            &_fields._value, /* use value instead of the union            */
            1U<<2,           /* we add 1 to _readers_present counter      */
            __ATOMIC_RELAXED ); /* don't produce a memory fence right now */

      bool acquired = !(current._writer_waiting | current._writer_present);
      if( !acquired ) {
         // We cannot lock the mutex right now. Rollback.
         rw_fields updated;
         updated._value = __atomic_fetch_sub(
               &_fields._value, /* use value instead of the union            */
               1U<<2,           /* rollback (sub 1) _readers_present counter */
               __ATOMIC_RELAXED ); /* don't produce a memory fence right now */

         // Now a regular read lock follows
         while(!acquired) {
            // We expect that no writers are neither present
            // nor waiting. If there are, the CAS atomic operation
            // will fail.
            current._writer_waiting  = false;
            current._writer_present  = false;
            updated = current;
            updated._readers_present++;

            acquired = __atomic_compare_exchange_n (
                  &_fields._value,/* destination    */
                  &current._value,/* expected value */
                  updated._value, /* desired value  */
                  true/*weak version, more efficient than strong if in loop*/,
                  __ATOMIC_ACQUIRE, /*success memorder*/
                  __ATOMIC_RELAXED);/*failure memorder*/
         }
      } else {
         // Prediction hit. Now we issue the barrier.
         __atomic_thread_fence (__ATOMIC_ACQUIRE);
      }
   }
  
   bool read_try_lock() {
     // Check no writers are present or waiting
     // If none, lock is acquired and _readers_present counter is increased

     rw_fields current, updated;
     current._value = __atomic_load_n( &_fields._value, __ATOMIC_RELAXED );

     // We expect that no writers are neither present
     // nor waiting. If there are, the CAS atomic operation
     // will fail.
     current._writer_waiting = false;
     current._writer_present = false;
     updated = current;
     updated._readers_present++;

     bool success = __atomic_compare_exchange_n (
       &_fields._value,/* destination    */
       &current._value,/* expected value */
       updated._value, /* desired value  */
       false/*strong version, better when loop is not necessary*/,
       __ATOMIC_ACQUIRE/*success memorder*/,
       __ATOMIC_RELAXED/*failure memorder*/ );

     return success;
   }

   void write_unlock() {
     // Unset _writer_present
     // Generate mask with all bits set except _writer_present flag
     // (the one we want to unset).
     rw_fields updated;
     updated._value = ~0U;
     updated._writer_present = 0;
     __atomic_fetch_and( &_fields._value, updated._value, __ATOMIC_RELEASE );
   }

   void write_lock() {
      // Atomically sets _writer_waiting
      // Wait until no readers nor writers are present
      rw_fields current, updated;

      bool success = false;
      do {
         // Set _writer_waiting. Skip if already set.
         if( !current._writer_waiting ) {
            // Generate mask with all bits unset except _writer_waiting flag
            // (the one we want to set).
            current._value = 0;
            current._writer_waiting = 1;
            current._value = __atomic_or_fetch( &_fields._value, current._value, __ATOMIC_RELAXED );
         }

         // Wait until no readers nor writers present.
         // We expect that no writers nor reads are present.
         // If there are, the CAS atomic operation will fail.
         // If lock is acquired, _writer_waiting is also unset
         current._writer_present  = false;
         current._readers_present = 0;
         updated = current;
         updated._writer_waiting = false;
         updated._writer_present = true;

         success = __atomic_compare_exchange_n (
               &_fields._value,/* destination    */
               &current._value,/* expected value */
               updated._value, /* desired value  */
               true/*weak version, more efficient than strong if in loop*/,
               __ATOMIC_ACQUIRE, /*success memorder*/
               __ATOMIC_RELAXED);/*failure memorder*/
      } while(!success);
   }
  
   bool write_try_lock() {
     // Check no readers or writers are neither present nor waiting.
     // If none, lock is acquired and _writer_present counter is set.

     rw_fields current, updated;
     current._value = __atomic_load_n( &_fields._value, __ATOMIC_RELAXED );

     // We expect that no writers are neither present nor waiting. If there
     // are, the CAS atomic operation will fail.
     current._writer_waiting = false;
     current._writer_present = false;
     current._readers_present = 0;
     updated = current;
     updated._writer_present = true;

     bool success = __atomic_compare_exchange_n (
       &_fields._value,/* destination    */
       &current._value,/* expected value */
       updated._value, /* desired value  */
       false/*strong version, better when loop is not necessary*/,
       __ATOMIC_ACQUIRE/*success memorder*/,
       __ATOMIC_RELAXED/*failure memorder*/ );

     return success;
   }
  
private:
   rw_fields _fields;
};

// Following classes allows locking/unlocking as a regular mutex so that it can
// be used with RAII locking classes such as std::lock_guard or
// std::unique_lock.
// Constructors are not explicit, so that implicit conversion is possible.
//
// Example usage:
//
// rw_spin_mutex mutex;
// reader_adaptor rdmutex(mutex);
// std::unique_lock<reader_adaptor> guard(rdmutex);
//
// or...
//
// writer_adaptor wrmutex(mutex);
// std::unique_lock<writer_adaptor> guard(wrmutex);
//
class reader_adaptor {
public:
   reader_adaptor( rw_spin_mutex& mutex ) :
      _mutex(mutex)
   {
   }

   // This class is non-copyable
   reader_adaptor( const reader_adaptor& ) = delete;

   void lock() {
      _mutex.read_lock();
   }

   bool try_lock() {
      return _mutex.read_try_lock();
   }

   void unlock() {
      _mutex.read_unlock();
   }

private:
   rw_spin_mutex& _mutex;
};

class writer_adaptor {
public:
   writer_adaptor( rw_spin_mutex& mutex ) :
      _mutex(mutex)
   {
   }

   // This class is non-copyable
   writer_adaptor( const writer_adaptor& ) = delete;

   void lock() {
      _mutex.write_lock();
   }

   bool try_lock() {
      return _mutex.write_try_lock();
   }

   void unlock() {
      _mutex.write_unlock();
   }

private:
   rw_spin_mutex& _mutex;
};

#endif // SPIN_MUTEX_HPP

