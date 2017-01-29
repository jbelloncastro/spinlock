#include "rw_spin_mutex.hpp"
#include "spin_mutex.hpp"

#include <iostream>
#include <mutex>
#include <thread>
#include <vector>

#include <unistd.h>

int main() {
   // Goal, use a shared STL vector. Concurrent reads are safe.
   // However, writes can reallocate the backend storage, so
   // read and write operations must be mutually exclusive.

   // Initialize vector with 1 element
   std::vector<int> shared( 1, 0);

   // Declare readwrite mutex to access the vector safely
#ifdef USE_RWLOCK
   rw_spin_mutex vector_mutex;
   reader_adaptor read_mutex (vector_mutex);
   writer_adaptor write_mutex(vector_mutex);
#else
   spin_mutex vector_mutex;
   typedef spin_mutex reader_adaptor;
   typedef spin_mutex writer_adaptor;
   spin_mutex& read_mutex (vector_mutex);
   spin_mutex& write_mutex(vector_mutex);
#endif

   auto read_operations = [&]() {
      long sum = 0;
      // Access elements of the vector
      // Size is updated, because insertions are performed
      // concurrently.
      for( int i = 0; i < 800000; ++i ) {
         std::lock_guard<reader_adaptor> guard(read_mutex);
         sum += shared[i%shared.size()];
      }
   };

   unsigned writers = 0;
   unsigned insertions_each = 25;
   auto write_operations = [&]() {
      // Insert new elements to the vector
      for( int i = 0; i < insertions_each; ++i ) {
         std::lock_guard<writer_adaptor> guard(write_mutex);
         shared.push_back(i);
      }
   };

   unsigned len = std::thread::hardware_concurrency();
   std::vector<std::thread> threads;
   threads.reserve(len);
   for( unsigned t = 0; t < len-writers; ++t )
      threads.emplace_back(read_operations);
   for( unsigned t = 0; t < writers; ++t ) {

      threads.emplace_back(write_operations);
   }

   for( std::thread& t : threads )
      t.join();

   bool success = shared.size() == writers*insertions_each+1;
   if( success )
      std::cout << "Success!" << std::endl;
   else
      std::cout << "Failed!" << std::endl;
   return 0;
}
