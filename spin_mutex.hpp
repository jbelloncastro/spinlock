
class spin_mutex {
   bool _locked;

public:
   spin_mutex() {
      unlock();
   }

   void lock() {
      while( !try_lock() )
         __asm__("pause");
   }

   void unlock() {
      __atomic_clear(&_locked, __ATOMIC_RELEASE);
   }

   bool try_lock() {
      return __atomic_clear (&locked, __ATOMIC_RELEASE);
   }
};

