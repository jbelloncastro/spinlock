#include "rw_spin_mutex.hpp"

#include <iostream>
#include <mutex>
#include <thread>
#include <vector>

int main() {
   // Goal, use a shared STL vector. Concurrent reads are safe.
   // However, writes can reallocate the backend storage, so
   // read and write operations must be mutually exclusive.

   // Initialize vector with 1 element
   std::vector<int> shared( 1, 0);

   // Declare readwrite mutex to access the vector safely
   rw_spin_mutex vector_mutex;

   auto read_operations = [&]() {
      reader_adaptor read_mutex(vector_mutex);
      long sum = 0;
      // Access elements of the vector
      // Size is updated, because insertions are performed
      // concurrently.
      for( int i = 0; i < 1000000; ++i ) {
         std::lock_guard<reader_adaptor> guard(read_mutex);
         sum += shared[i%shared.size()];
      }
   };

   auto write_operations = [&]() {
      writer_adaptor write_mutex(vector_mutex);
      // Insert new elements to the vector
      for( int i = 0; i < 100000; ++i ) {
         std::lock_guard<writer_adaptor> guard(write_mutex);
         shared.push_back(i);
      }
   };

   unsigned writers = 2;
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

   bool success = shared.size() == writers*100000+1;
   if( success )
      std::cout << "Success!" << std::endl;
   else
      std::cout << "Failed!" << std::endl;
   return 0;
}
