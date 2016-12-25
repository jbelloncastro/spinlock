
class ticket_mutex {
   unsigned long _next;
   unsigned long _last;

public:
   ticket_mutex() {
      const unsigned long zero = 0U;
	   __atomic_store(&_next, &zero, __ATOMIC_RELEASE );
	   __atomic_store(&_last, &zero, __ATOMIC_RELEASE );
   }

   void lock() {
      unsigned ticket, next;
      ticket = __atomic_fetch_add (&_last, 1U, __ATOMIC_ACQ_REL);
      __atomic_load(&_next, &next, __ATOMIC_ACQUIRE);

      while( ticket != next ) {
         unsigned backoff = BACKOFF_CYCLES;
         while( backoff-- )
            __asm__("pause");
         __atomic_load(&_next, &next, __ATOMIC_ACQUIRE);
      }
   }

   void unlock() {
      __atomic_fetch_add(&_next, 1U, __ATOMIC_RELEASE);
   }

   bool try_lock() {
      unsigned ticket, next;
      ticket = __atomic_fetch_add(&_last, 1U, __ATOMIC_RELAXED);
      __atomic_load(&_next, &next, __ATOMIC_ACQUIRE);
      return ticket == next;
   }
};

