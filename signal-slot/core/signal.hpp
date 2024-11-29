#pragma once

#include <atomic>
#include <cstring>
#include <future>
#include <memory>
#include <mutex>
#include <type_traits>
#include <utility>
#include <thread>
#include <vector>

#if defined(__GXX_RTTI) || defined(__cpp_rtti) || defined(_CPPRTTI)
#define SIGSLOT_RTTI_ENABLED 1
#include <typeinfo>
#endif

#include <iostream>
#include <assert.h>

namespace core {
    class TaskQueue;
}

namespace sigslot {
    //class i_executor;

    template <typename, typename...>
    class signal_base;

    namespace detail {

        // Used to detect an object of observer type
        struct observer_type {};

    } // namespace detail

    namespace trait {

        /// represent a list of types
        template <typename...> struct typelist {};

        /**
         * Pointers that can be converted to a weak pointer concept for tracking
         * purpose must implement the to_weak() function in order to make use of
         * ADL to convert that type and make it usable
         */

        template <typename T>
        std::weak_ptr<T> to_weak(std::weak_ptr<T> w) {
            return w;
        }

        template <typename T>
        std::weak_ptr<T> to_weak(std::shared_ptr<T> s) {
            return s;
        }

        // tools
        namespace detail {

            template <typename...>
            struct voider { using type = void; };

            // void_t from c++17
            template <typename...T>
            using void_t = typename detail::voider<T...>::type;

            template <typename, typename = void>
            struct has_call_operator : std::false_type {};

            template <typename F>
            struct has_call_operator<F, void_t<decltype(&std::remove_reference<F>::type::operator())>>
            : std::true_type {};

            template <typename, typename, typename = void, typename = void>
            struct is_callable : std::false_type {};

            template <typename P, typename F, typename... T>
            struct is_callable<P, F, typelist<T...>,
                               void_t<decltype(((*std::declval<P>()).*std::declval<F>())(std::declval<T>()...))>>
            : std::true_type {};

            template <typename F, typename... T>
            struct is_callable<F, typelist<T...>,
                               void_t<decltype(std::declval<F>()(std::declval<T>()...))>>
            : std::true_type {};


            template <typename T, typename = void>
            struct is_weak_ptr : std::false_type {};

            template <typename T>
            struct is_weak_ptr<T, void_t<decltype(std::declval<T>().expired()),
                                         decltype(std::declval<T>().lock()),
                                         decltype(std::declval<T>().reset())>>
            : std::true_type {};

            template <typename T, typename = void>
            struct is_weak_ptr_compatible : std::false_type {};

            template <typename T>
            struct is_weak_ptr_compatible<T, void_t<decltype(to_weak(std::declval<T>()))>>
            : is_weak_ptr<decltype(to_weak(std::declval<T>()))> {};

            template <typename...>
            struct is_signal : std::false_type {};

            template <typename L, typename... T>
            struct is_signal<signal_base<L, T...>>
            : std::true_type {};

        } // namespace detail

        static constexpr bool with_rtti =
#ifdef SIGSLOT_RTTI_ENABLED
            true;
#else
            false;
#endif

        /// determine if a pointer is convertible into a "weak" pointer
        template <typename P>
        constexpr bool is_weak_ptr_compatible_v = detail::is_weak_ptr_compatible<std::decay_t<P>>::value;

        /// determine if a type T (Callable or Pmf) is callable with supplied arguments
        template <typename L, typename... T>
        constexpr bool is_callable_v = detail::is_callable<T..., L>::value;

        template <typename T>
        constexpr bool is_weak_ptr_v = detail::is_weak_ptr<T>::value;

        template <typename T>
        constexpr bool has_call_operator_v = detail::has_call_operator<T>::value;

        template <typename T>
        constexpr bool is_pointer_v = std::is_pointer<T>::value;

        template <typename T>
        constexpr bool is_func_v = std::is_function<T>::value;

        template <typename T>
        constexpr bool is_pmf_v = std::is_member_function_pointer<T>::value;

        template <typename T>
        constexpr bool is_observer_v = std::is_base_of<::sigslot::detail::observer_type,
                                                       std::remove_pointer_t<std::remove_reference_t<T>>>::value;

        template <typename S>
        constexpr bool is_signal_v = detail::is_signal<S>::value;

    } // namespace trait

    enum connection_type {
        auto_connection = 0,
        direct_connection = 1,
        queued_connection = 2,
        blocking_queued_connection = 3,
        unique_connection = 0x80,
        singleshot_connection = 0x100
    };

    /**
     * A group_id is used to identify a group of slots
     */
    using group_id = std::int32_t;

    namespace detail {

        /**
         * The following function_traits and object_pointer series of templates are
         * used to circumvent the type-erasing that takes place in the slot_base
         * implementations. They are used to compare the stored functions and objects
         * with another one for disconnection purpose.
         */

        /*
         * Function pointers and member function pointers size differ from compiler to
         * compiler, and for virtual members compared to non virtual members. On some
         * compilers, multiple inheritance has an impact too. Hence, we form an union
         * big enough to store any kind of function pointer.
         */
        namespace mock {

            struct a { virtual ~a() = default; void f(); virtual void g(); static void h(); };
            struct b { virtual ~b() = default; void f(); virtual void g(); };
            struct c : a, b { void f(); void g() override; };
            struct d : virtual a { void g() override; };

            union fun_types {
                decltype(&d::g) dm;
                decltype(&c::g) mm;
                decltype(&c::g) mvm;
                decltype(&a::f) m;
                decltype(&a::g) vm;
                decltype(&a::h) s;
                void (*f)();
                void *o;
            };

        } // namespace mock

        /*
         * This struct is used to store function pointers.
         * This is needed for slot disconnection by function pointer.
         * It assumes the underlying implementation to be trivially copiable.
         */
        struct func_ptr {
            func_ptr()
            : sz{0}
            {
                std::uninitialized_fill(std::begin(data), std::end(data), '\0');
            }

            template <typename T>
            void store(const T& t) {
                const auto *b = reinterpret_cast<const char*>(&t);
                sz = sizeof(T);
                std::memcpy(data, b, sz);
            }

            template <typename T>
            const T* as() const {
                if (sizeof(T) != sz) {
                    return nullptr;
                }
                return reinterpret_cast<const T*>(data);
            }

        private:
            alignas(sizeof(mock::fun_types)) char data[sizeof(mock::fun_types)];
            size_t sz;
        };


        template <typename T, typename = void>
        struct function_traits {
            static void ptr(const T& /*t*/, func_ptr& /*d*/) {
            }

            static bool eq(const T& /*t*/, const func_ptr& /*d*/) {
                return false;
            }

            static constexpr bool is_disconnectable = false;
            static constexpr bool must_check_object = true;
        };

        template <typename T>
        struct function_traits<T, std::enable_if_t<trait::is_func_v<T>>> {
            static void ptr(T& t, func_ptr& d) {
                d.store(&t);
            }

            static bool eq(T& t, const func_ptr& d) {
                const auto *r = d.as<const T*>();
                return r && *r == &t;
            }

            static constexpr bool is_disconnectable = true;
            static constexpr bool must_check_object = false;
        };

        template <typename T>
        struct function_traits<T*, std::enable_if_t<trait::is_func_v<T>>> {
            static void ptr(T *t, func_ptr& d) {
                function_traits<T>::ptr(*t, d);
            }

            static bool eq(T *t, const func_ptr& d) {
                return function_traits<T>::eq(*t, d);
            }

            static constexpr bool is_disconnectable = true;
            static constexpr bool must_check_object = false;
        };

        template <typename T>
        struct function_traits<T, std::enable_if_t<trait::is_pmf_v<T>>> {
            static void ptr(T t, func_ptr& d) {
                d.store(t);
            }

            static bool eq(T t, const func_ptr& d) {
                const auto *r = d.as<const T>();
                return r && *r == t;
            }

            static constexpr bool is_disconnectable = trait::with_rtti;
            static constexpr bool must_check_object = true;
        };

        // for function objects, the assumption is that we are looking for the call operator
        template <typename T>
        struct function_traits<T, std::enable_if_t<trait::has_call_operator_v<T>>> {
            using call_type = decltype(&std::remove_reference<T>::type::operator());

            static void ptr(const T& /*t*/, func_ptr& d) {
                function_traits<call_type>::ptr(&T::operator(), d);
            }

            static bool eq(const T& /*t*/, const func_ptr& d) {
                return function_traits<call_type>::eq(&T::operator(), d);
            }

            static constexpr bool is_disconnectable = function_traits<call_type>::is_disconnectable;
            static constexpr bool must_check_object = function_traits<call_type>::must_check_object;
        };

        template <typename T>
        func_ptr get_function_ptr(const T& t) {
            func_ptr d;
            function_traits<std::decay_t<T>>::ptr(t, d);
            return d;
        }

        template <typename T>
        bool eq_function_ptr(const T& t, const func_ptr& d) {
            return function_traits<std::decay_t<T>>::eq(t, d);
        }

        /*
         * obj_ptr is used to store a pointer to an object.
         * The object_pointer traits are needed to handle trackable objects correctly,
         * as they are likely to not be pointers.
         */
        using obj_ptr = const void*;

        template <typename T>
        obj_ptr get_object_ptr(const T& t);

        template <typename T, typename = void>
        struct object_pointer {
            static obj_ptr get(const T&) {
                return nullptr;
            }
        };

        template <typename T>
        struct object_pointer<T*, std::enable_if_t<trait::is_pointer_v<T*>>> {
            static obj_ptr get(const T *t) {
                return reinterpret_cast<obj_ptr>(t);
            }
        };

        template <typename T>
        struct object_pointer<T, std::enable_if_t<trait::is_weak_ptr_v<T>>> {
            static obj_ptr get(const T& t) {
                auto p = t.lock();
                return get_object_ptr(p);
            }
        };

        template <typename T>
        struct object_pointer<T, std::enable_if_t<!trait::is_pointer_v<T> &&
                                                  !trait::is_weak_ptr_v<T> &&
                                                  trait::is_weak_ptr_compatible_v<T>>>
        {
            static obj_ptr get(const T& t) {
                return t ? reinterpret_cast<obj_ptr>(t.get()) : nullptr;
            }
        };

        template <typename T>
        obj_ptr get_object_ptr(const T& t) {
            return object_pointer<T>::get(t);
        }


        // noop mutex for thread-unsafe use
        struct null_mutex {
            null_mutex() noexcept = default;
            ~null_mutex() noexcept = default;
            null_mutex(const null_mutex&) = delete;
            null_mutex& operator=(const null_mutex&) = delete;
            null_mutex(null_mutex&&) = delete;
            null_mutex& operator=(null_mutex&&) = delete;

            inline bool try_lock() noexcept { return true; }
            inline void lock() noexcept {}
            inline void unlock() noexcept {}
        };

        /**
         * A spin mutex that yields, mostly for use in benchmarks and scenarii that invoke
         * slots at a very high pace.
         * One should almost always prefer a standard mutex over this.
         */
        struct spin_mutex {
            spin_mutex() noexcept = default;
            ~spin_mutex() noexcept = default;
            spin_mutex(spin_mutex const&) = delete;
            spin_mutex& operator=(const spin_mutex&) = delete;
            spin_mutex(spin_mutex&&) = delete;
            spin_mutex& operator=(spin_mutex&&) = delete;

            void lock() noexcept {
                while (true) {
                    while (!state.load(std::memory_order_relaxed)) {
                        std::this_thread::yield();
                    }

                    if (try_lock()) {
                        break;
                    }
                }
            }

            bool try_lock() noexcept {
                return state.exchange(false, std::memory_order_acquire);
            }

            void unlock() noexcept {
                state.store(true, std::memory_order_release);
            }

        private:
            std::atomic<bool> state {true};
        };

        /**
         * A simple copy on write container that will be used to improve slot lists
         * access efficiency in a multithreaded context.
         */
        template <typename T>
        class copy_on_write {
            struct payload {
                payload() = default;

                template <typename... Args>
                explicit payload(Args&& ...args)
                : value(std::forward<Args>(args)...)
                {}

                std::atomic<std::size_t> count{1};
                T value;
            };

        public:
            using element_type = T;

            copy_on_write()
            : m_data(new payload)
            {}

            template <typename U>
            explicit copy_on_write(U&& x, std::enable_if_t<!std::is_same<std::decay_t<U>,
                                                                         copy_on_write>::value>* = nullptr)
            : m_data(new payload(std::forward<U>(x)))
            {}

            copy_on_write(const copy_on_write& x) noexcept
            : m_data(x.m_data)
            {
                ++m_data->count;
            }

            copy_on_write(copy_on_write&& x) noexcept
            : m_data(x.m_data)
            {
                x.m_data = nullptr;
            }

            ~copy_on_write() {
                if (m_data && (--m_data->count == 0)) {
                    delete m_data;
                }
            }

            copy_on_write& operator=(const copy_on_write& x) noexcept {
                if (&x != this) {
                    *this = copy_on_write(x);
                }
                return *this;
            }

            copy_on_write& operator=(copy_on_write&& x) noexcept  {
                auto tmp = std::move(x);
                swap(*this, tmp);
                return *this;
            }

            element_type& write() {
                if (!unique()) {
                    *this = copy_on_write(read());
                }
                return m_data->value;
            }

            const element_type& read() const noexcept {
                return m_data->value;
            }

            friend inline void swap(copy_on_write& x, copy_on_write& y) noexcept {
                using std::swap;
                swap(x.m_data, y.m_data);
            }

        private:
            bool unique() const noexcept {
                return m_data->count == 1;
            }

        private:
            payload *m_data;
        };

        /**
         * Specializations for thread-safe code path
         */
        template <typename T>
        const T& cow_read(const T& v) {
            return v;
        }

        template <typename T>
        const T& cow_read(copy_on_write<T>& v) {
            return v.read();
        }

        template <typename T>
        T& cow_write(T& v) {
            return v;
        }

        template <typename T>
        T& cow_write(copy_on_write<T>& v) {
            return v.write();
        }

/**
 * std::make_shared instantiates a lot a templates, and makes both compilation time
 * and executable size far bigger than they need to be. We offer a make_shared
 * equivalent that will avoid most instantiations with the following tradeoffs:
 * - Not exception safe,
 * - Allocates a separate control block, and will thus make the code slower.
 */
#ifdef SIGSLOT_REDUCE_COMPILE_TIME
        template <typename B, typename D, typename ...Arg>
        inline std::shared_ptr<B> make_shared(Arg&& ... arg) {
            return std::shared_ptr<B>(static_cast<B*>(new D(std::forward<Arg>(arg)...)));
        }
#else
        template <typename B, typename D, typename ...Arg>
        inline std::shared_ptr<B> make_shared(Arg&& ... arg) {
            return std::static_pointer_cast<B>(std::make_shared<D>(std::forward<Arg>(arg)...));
        }
#endif


        // Adapt a signal into a cheap function object, for easy signal chaining
        template <typename SigT>
        struct signal_wrapper {
            template <typename... U>
            void operator()(U&& ...u) {
                (*m_sig)(std::forward<U>(u)...);
            }

            SigT *m_sig{};
        };


        /* slot_state holds slot type independent state, to be used to interact with
         * slots indirectly through connection and scoped_connection objects.
         */
        class slot_state {
        public:
            constexpr slot_state(group_id gid) noexcept
            : m_index(0)
            , m_group(gid)
            , m_connected(true)
            , m_blocked(false)
            {}

            virtual ~slot_state() = default;

            virtual bool connected() const noexcept { return m_connected; }

            bool disconnect() noexcept {
                bool ret = m_connected.exchange(false);
                if (ret) {
                    do_disconnect();
                }
                return ret;
            }

            bool blocked() const noexcept { return m_blocked.load(); }
            void block()   noexcept { m_blocked.store(true); }
            void unblock() noexcept { m_blocked.store(false); }

        protected:
            virtual void do_disconnect() {}

            auto index() const {
                return m_index;
            }

            auto& index() {
                return m_index;
            }

            group_id group() const {
                return m_group;
            }

        private:
            template <typename, typename...>
            friend class ::sigslot::signal_base;

            std::size_t m_index;     // index into the array of slot pointers inside the signal
            const group_id m_group;  // slot group this slot belongs to
            std::atomic<bool> m_connected;
            std::atomic<bool> m_blocked;
        };

    } // namespace detail

    /**
     * connection_blocker is a RAII object that blocks a connection until destruction
     */
    class connection_blocker {
    public:
        connection_blocker() = default;
        ~connection_blocker() noexcept { release(); }

        connection_blocker(const connection_blocker&) = delete;
        connection_blocker& operator=(const connection_blocker&) = delete;

        connection_blocker(connection_blocker&& o) noexcept
        : m_state{std::move(o.m_state)}
        {}

        connection_blocker& operator=(connection_blocker&& o) noexcept {
            release();
            m_state.swap(o.m_state);
            return *this;
        }

    private:
        friend class connection;
        explicit connection_blocker(std::weak_ptr<detail::slot_state> s) noexcept
        : m_state{std::move(s)}
        {
            if (auto d = m_state.lock()) {
                d->block();
            }
        }

        void release() noexcept {
            if (auto d = m_state.lock()) {
                d->unblock();
            }
        }

    private:
        std::weak_ptr<detail::slot_state> m_state;
    };


    /**
     * A connection object allows interaction with an ongoing slot connection
     *      * It allows common actions such as connection blocking and disconnection.
     * Note that connection is not a RAII object, one does not need to hold one
     * such object to keep the signal-slot connection alive.
     */
    class connection {
    public:
        connection() = default;
        virtual ~connection() = default;

        connection(const connection&) noexcept = default;
        connection& operator=(const connection&) noexcept = default;
        connection(connection&&) noexcept = default;
        connection& operator=(connection&&) noexcept = default;

        bool valid() const noexcept {
            return !m_state.expired();
        }

        bool connected() const noexcept {
            const auto d = m_state.lock();
            return d && d->connected();
        }

        bool disconnect() noexcept {
            auto d = m_state.lock();
            return d && d->disconnect();
        }

        bool blocked() const noexcept {
            const auto d = m_state.lock();
            return d && d->blocked();
        }

        void block() noexcept {
            if (auto d = m_state.lock()) {
                d->block();
            }
        }

        void unblock() noexcept {
            if (auto d = m_state.lock()) {
                d->unblock();
            }
        }

        connection_blocker blocker() const noexcept {
            return connection_blocker{m_state};
        }

    protected:
        template <typename, typename...> friend class signal_base;
        explicit connection(std::weak_ptr<detail::slot_state> s) noexcept
        : m_state{std::move(s)}
        {}

    protected:
        std::weak_ptr<detail::slot_state> m_state;
    };

    /**
     * scoped_connection is a RAII version of connection
     * It disconnects the slot from the signal upon destruction.
     */
    class scoped_connection final : public connection {
    public:
        scoped_connection() = default;
        ~scoped_connection() override {
            disconnect();
        }

        /*implicit*/ scoped_connection(const connection& c) noexcept : connection(c) {}
        /*implicit*/ scoped_connection(connection&& c) noexcept : connection(std::move(c)) {}

        scoped_connection(const scoped_connection&) noexcept = delete;
        scoped_connection& operator=(const scoped_connection&) noexcept = delete;

        scoped_connection(scoped_connection&& o) noexcept
        : connection{std::move(o.m_state)}
        {}

        scoped_connection& operator=(scoped_connection&& o) noexcept {
            disconnect();
            m_state.swap(o.m_state);
            return *this;
        }

    private:
        template <typename, typename...> friend class signal_base;
        explicit scoped_connection(std::weak_ptr<detail::slot_state> s) noexcept
        : connection{std::move(s)}
        {}
    };

    /**
     * Observer is a base class for intrusive lifetime tracking of objects.
     *      * This is an alternative to trackable pointers, such as std::shared_ptr,
     * and manual connection management by keeping connection objects in scope.
     * Deriving from this class allows automatic disconnection of all the slots
     * connected to any signal when an instance is destroyed.
     */
    template <typename Lockable>
    struct observer_base : private detail::observer_type {
        virtual ~observer_base() = default;

    protected:
        /**
         * Disconnect all signals connected to this object.
         *          * To avoid invocation of slots on a semi-destructed instance, which may happen
         * in multi-threaded contexts, derived classes should call this method in their
         * destructor. This will ensure proper disconnection prior to the destruction.
         */
        void disconnect_all() {
            std::unique_lock<Lockable> _{m_mutex};
            m_connections.clear();
        }

    private:
        template <typename, typename ...>
        friend class signal_base;

        void add_connection(connection conn) {
            std::unique_lock<Lockable> _{m_mutex};
            m_connections.emplace_back(std::move(conn));
        }

        Lockable m_mutex;
        std::vector<scoped_connection> m_connections;
    };

    /**
     * Specialization of observer_base to be used in single threaded contexts.
     */
    using observer_st = observer_base<detail::null_mutex>;

    /**
     * Specialization of observer_base to be used in multi-threaded contexts.
     */
    using observer = observer_base<std::mutex>;


    namespace detail {

        // interface for cleanable objects, used to cleanup disconnected slots
        struct cleanable {
            virtual ~cleanable() = default;
            virtual void clean(slot_state *) = 0;
        };

        template <typename...>
        class slot_base;

        template <typename... T>
        using slot_ptr = std::shared_ptr<slot_base<T...>>;


        /* A base class for slot objects. This base type only depends on slot argument
         * types, it will be used as an element in an intrusive singly-linked list of
         * slots, hence the public next member.
         */
        template <typename... Args>
        class slot_base : public slot_state {
        public:
            using base_types = trait::typelist<Args...>;

            explicit slot_base(cleanable& c, uint32_t type, core::TaskQueue* queue, group_id gid)
            : slot_state(gid)
            , m_cleaner(c)
            , m_queue(queue) {
                m_singleshot = type & connection_type::singleshot_connection;
                uint32_t t = type;
                t &= ~connection_type::unique_connection;
                t &= ~connection_type::singleshot_connection;
                m_type = t;
            }

            ~slot_base() override = default;

            // method effectively responsible for calling the "slot" function with
            // supplied arguments whenever emission happens.
            virtual void call_slot(Args...) = 0;

            template <typename... U>
            void operator()(U&& ...u) {
                if (slot_state::connected() && !slot_state::blocked()) {
                    call_slot(std::forward<U>(u)...);
                }
            }

            // check if we are storing callable c
            template <typename C>
            bool has_callable(const C& c) const {
                auto p = get_callable();
                return eq_function_ptr(c, p);
            }

            template <typename C>
            std::enable_if_t<function_traits<C>::must_check_object, bool>
            has_full_callable(const C& c) const {
                return has_callable(c) && check_class_type<std::decay_t<C>>();
            }

            template <typename C>
            std::enable_if_t<!function_traits<C>::must_check_object, bool>
            has_full_callable(const C& c) const {
                return has_callable(c);
            }

            // check if we are storing object o
            template <typename O>
            bool has_object(const O& o) const {
                return get_object() == get_object_ptr(o);
            }

            void set_unique(bool unique) {
                this->m_unique = unique;
            }

            bool is_unique() {
                return this->m_unique;
            }

        protected:
            void do_disconnect() final {
                m_cleaner.clean(this);
            }

            // retieve a pointer to the object embedded in the slot
            virtual obj_ptr get_object() const noexcept {
                return nullptr;
            }

            // retieve a pointer to the callable embedded in the slot
            virtual func_ptr get_callable() const noexcept {
                return get_function_ptr(nullptr);
            }

            inline bool can_emit() {
                if (this->m_singleshot && this->m_emitted) {
                    return false;
                }
                return true;
            }

            inline void set_emitted() {
                if (this->m_singleshot && !this->m_emitted) {
                    this->m_emitted = true;
                }
            }

            inline bool is_current() {
                // If no queue is specified, it is assumed that the current thread is directly calling the
                if (!this->m_queue) {
                    return true;
                }
                return this->m_queue->IsCurrent();
            }

            uint32_t type() {
                uint32_t type = this->m_type;
                if (type == connection_type::auto_connection) {
                    if (is_current()) {
                        type = connection_type::direct_connection;
                    } else {
                        type = connection_type::queued_connection;
                    }
                }
                return type;
            }

#ifdef SIGSLOT_RTTI_ENABLED
            // retieve a pointer to the callable embedded in the slot
            virtual const std::type_info& get_callable_type() const noexcept {
                return typeid(nullptr);
            }

        private:
            template <typename U>
            bool check_class_type() const {
                return typeid(U) == get_callable_type();
            }

#else
            template <typename U>
            bool check_class_type() const {
                return false;
            }
#endif
        protected:
            std::atomic<uint32_t> m_type = {0};
            std::atomic_bool m_unique = {false};
            core::TaskQueue* m_queue = nullptr;
            std::atomic_bool m_singleshot = {false};
            std::atomic_bool m_emitted = {false};

        private:
            cleanable& m_cleaner;
        };

        /*
         * A slot object holds state information, and a callable to to be called
         * whenever the function call operator of its slot_base base class is called.
         */
        template <typename Func, typename... Args>
        class slot final : public slot_base<Args...>, public std::enable_shared_from_this<slot<Func, Args...>> {
        public:
            using this_type = slot<Func, Args...>;
            template <typename F, typename Gid>
            constexpr slot(cleanable& c, F&& f, uint32_t type, core::TaskQueue* queue, Gid gid)
            : slot_base<Args...>(c, type, queue, gid)
            , func{std::forward<F>(f)} {}

        protected:
            void call_slot(Args ...args) override {
                if (!this->can_emit()) {
                    return;
                }
                this->set_emitted();
                uint32_t type = this->type();
                if (type == connection_type::direct_connection) {
                    if (this->slot_state::connected()) {
                        func(args...);
                        if (this->m_singleshot && this->m_emitted) {
                            this->slot_state::disconnect();
                        }
                    } else {
                        std::cerr << "canceling slot execution due to connection being disconnected" << std::endl;
                    }
                } else if (type == connection_type::queued_connection) {
                    assert(this->m_queue);
                    if (this->m_queue) {
                        this->m_queue->PostTask([wself = std::weak_ptr<this_type>(this_type::shared_from_this()), args...]() mutable {
                            auto self = wself.lock();
                            if (!self) {
                                return;
                            }
                            if (self->slot_state::connected()) {
                                self->func(args...);
                                if (self->m_singleshot && self->m_emitted) {
                                    self->slot_state::disconnect();
                                }
                            } else {
                                std::cerr << "canceling slot execution due to connection being disconnected" << std::endl;
                            }
                        });
                    } else {
                        std::cerr << "thread is nullptr" << std::endl;
                    }
                } else if (type == connection_type::blocking_queued_connection) {
                    auto promise = std::promise<void>();
                    assert(this->m_queue);
                    this->m_queue->PostTask([this, &args..., &promise]() mutable {
                        if (this->slot_state::connected()) {
                            this->func(args...);
                            if (this->m_singleshot && this->m_emitted) {
                                this->slot_state::disconnect();
                            }
                        } else {
                            std::cerr << "canceling slot execution due to connection being disconnected" << std::endl;
                        }
                        promise.set_value();
                    });
                    promise.get_future().get();
                } else {
                    std::cerr << "illegal connection type" << std::endl;
                }
            }

            func_ptr get_callable() const noexcept override {
                return get_function_ptr(func);
            }

#ifdef SIGSLOT_RTTI_ENABLED
            const std::type_info& get_callable_type() const noexcept override {
                return typeid(func);
            }
#endif

        private:
            std::decay_t<Func> func;
        };

        /*
         * Variation of slot that prepends a connection object to the callable
         */
        template <typename Func, typename... Args>
        class slot_extended final : public slot_base<Args...>, public std::enable_shared_from_this<slot_extended<Func, Args...>> {
        public:
            using this_type = slot_extended<Func, Args...>;
            template <typename F>
            constexpr slot_extended(cleanable& c, F&& f, uint32_t type, core::TaskQueue* queue, group_id gid)
            : slot_base<Args...>(c, type, queue, gid)
            , func{std::forward<F>(f)} {}

            connection conn;

        protected:
            void call_slot(Args ...args) override {
                if (!this->can_emit()) {
                    return;
                }
                this->set_emitted();
                uint32_t type = this->type();
                if (type == connection_type::direct_connection) {
                    if (this->slot_state::connected()) {
                        func(conn, args...);
                        if (this->m_singleshot && this->m_emitted) {
                            this->slot_state::disconnect();
                        }
                    } else {
                        std::cerr << "canceling slot execution due to connection being disconnected" << std::endl;
                    }
                } else if (type == connection_type::queued_connection) {
                    assert(this->m_queue);
                    if (this->m_queue) {
                        this->m_queue->PostTask([wself = std::weak_ptr<this_type>(this_type::shared_from_this()), args...]() mutable {
                            auto self = wself.lock();
                            if (!self) {
                                return;
                            }
                            if (self->slot_state::connected()) {
                                self->func(self->conn, args...);
                                if (self->m_singleshot && self->m_emitted) {
                                    self->slot_state::disconnect();
                                }
                            } else {
                                std::cerr << "canceling slot execution due to connection being disconnected" << std::endl;
                            }
                        });
                    } else {
                        std::cerr << "thread is nullptr" << std::endl;
                    }
                } else if (type == connection_type::blocking_queued_connection) {
                    auto promise = std::promise<void>();
                    assert(this->m_queue);
                    this->m_queue->PostTask([this, &args..., &promise]() mutable {
                        if (this->slot_state::connected()) {
                            this->func(this->conn, args...);
                            if (this->m_singleshot && this->m_emitted) {
                                this->slot_state::disconnect();
                            }
                        } else {
                            std::cerr << "canceling slot execution due to connection being disconnected" << std::endl;
                        }
                        promise.set_value();
                    });
                    promise.get_future().get();
                } else {
                    std::cerr << "illegal connection type" << std::endl;
                }
            }

            func_ptr get_callable() const noexcept override {
                return get_function_ptr(func);
            }

#ifdef SIGSLOT_RTTI_ENABLED
            const std::type_info& get_callable_type() const noexcept override {
                return typeid(func);
            }
#endif

        private:
            std::decay_t<Func> func;
        };

        /*
         * A slot object holds state information, an object and a pointer over member
         * function to be called whenever the function call operator of its slot_base
         * base class is called.
         */
        template <typename Ptr, typename Pmf, typename... Args>
        class slot_pmf final : public slot_base<Args...>, public std::enable_shared_from_this<slot_pmf<Ptr, Pmf, Args...>> {
        public:
            using this_type = slot_pmf<Ptr, Pmf, Args...>;
            template <typename P, typename F>
            constexpr slot_pmf(cleanable& c, P&& p, F&& f, uint32_t type, core::TaskQueue* queue, group_id gid)
            : slot_base<Args...>(c, type, queue, gid)
            , ptr{std::forward<P>(p)}
            , pmf{std::forward<F>(f)} {}

        protected:
            void call_slot(Args ...args) override {
                if (!this->can_emit()) {
                    return;
                }
                this->set_emitted();
                uint32_t type = this->type();
                if (type == connection_type::direct_connection) {
                    if (this->slot_state::connected()) {
                        ((*ptr).*pmf)(args...);
                        if (this->m_singleshot && this->m_emitted) {
                            this->slot_state::disconnect();
                        }
                    } else {
                        std::cerr << "canceling slot execution due to connection being disconnected" << std::endl;
                    }
                } else if (type == connection_type::queued_connection) {
                    assert(this->m_queue);
                    if (this->m_queue) {
                        this->m_queue->PostTask([wself = std::weak_ptr<this_type>(this_type::shared_from_this()), args...]() mutable {
                            auto self = wself.lock();
                            if (!self) {
                                return;
                            }
                            if (self->slot_state::connected()) {
                                ((*(self->ptr)).*(self->pmf))(args...);
                                if (self->m_singleshot && self->m_emitted) {
                                    self->slot_state::disconnect();
                                }
                            } else {
                                std::cerr << "canceling slot execution due to connection being disconnected" << std::endl;
                            }
                        });
                    } else {
                        std::cerr << "thread is nullptr" << std::endl;
                    }
                } else if (type == connection_type::blocking_queued_connection) {
                    auto promise = std::promise<void>();
                    assert(this->m_queue);
                    this->m_queue->PostTask([this, &args..., &promise]() mutable {
                        if (this->slot_state::connected()) {
                            ((*(this->ptr)).*(this->pmf))(args...);
                            if (this->m_singleshot && this->m_emitted) {
                                this->slot_state::disconnect();
                            }
                        } else {
                            std::cerr << "canceling slot execution due to connection being disconnected" << std::endl;
                        }
                        promise.set_value();
                    });
                    promise.get_future().get();
                } else {
                    std::cerr << "illegal connection type" << std::endl;
                }
            }

            func_ptr get_callable() const noexcept override {
                return get_function_ptr(pmf);
            }

            obj_ptr get_object() const noexcept override {
                return get_object_ptr(ptr);
            }

#ifdef SIGSLOT_RTTI_ENABLED
            const std::type_info& get_callable_type() const noexcept override {
                return typeid(pmf);
            }
#endif

        private:
            std::decay_t<Ptr> ptr;
            std::decay_t<Pmf> pmf;
        };

        /*
         * Variation of slot that prepends a connection object to the callable
         */
        template <typename Ptr, typename Pmf, typename... Args>
        class slot_pmf_extended final : public slot_base<Args...>, public std::enable_shared_from_this<slot_pmf_extended<Ptr, Pmf, Args...>> {
        public:
            using this_type = slot_pmf_extended<Pmf, Ptr, Args...>;
            template <typename P, typename F>
            constexpr slot_pmf_extended(cleanable& c, P&& p, F&& f, uint32_t type, core::TaskQueue* executor, group_id gid)
            : slot_base<Args...>(c, type, executor, gid)
            , ptr{std::forward<P>(p)}
            , pmf{std::forward<F>(f)} {}

            connection conn;

        protected:
            void call_slot(Args ...args) override {
                if (!this->can_emit()) {
                    return;
                }
                this->set_emitted();
                uint32_t type = this->type();
                if (type == connection_type::direct_connection) {
                    if (this->slot_state::connected()) {
                        ((*ptr).*pmf)(conn, args...);
                        if (this->m_singleshot && this->m_emitted) {
                            this->slot_state::disconnect();
                        }
                    } else {
                        std::cerr << "canceling slot execution due to connection being disconnected" << std::endl;
                    }
                } else if (type == connection_type::queued_connection) {
                    assert(this->m_queue);
                    if (this->m_queue) {
                        this->m_queue->PostTask([wself = std::weak_ptr<this_type>(this_type::shared_from_this()), args...]() mutable {
                            auto self = wself.lock();
                            if (!self) {
                                return;
                            }
                            if (self->slot_state::connected()) {
                                ((*(self->ptr)).*(self->pmf))(self->conn, args...);
                                if (self->m_singleshot && self->m_emitted) {
                                    self->slot_state::disconnect();
                                }
                            } else {
                                std::cerr << "canceling slot execution due to connection being disconnected" << std::endl;
                            }
                        });
                    } else {
                        std::cerr << "thread is nullptr" << std::endl;
                    }
                } else if (type == connection_type::blocking_queued_connection) {
                    auto promise = std::promise<void>();
                    assert(this->m_queue);
                    this->m_queue->PostTask([this, &args..., &promise]() mutable {
                        if (this->slot_state::connected()) {
                            ((*(this->ptr)).*(this->pmf))(this->conn, args...);
                            if (this->m_singleshot && this->m_emitted) {
                                this->slot_state::disconnect();
                            }
                        } else {
                            std::cerr << "canceling slot execution due to connection being disconnected" << std::endl;
                        }
                        promise.set_value();
                    });
                    promise.get_future().get();
                } else {
                    std::cerr << "illegal connection type" << std::endl;
                }
            }

            func_ptr get_callable() const noexcept override {
                return get_function_ptr(pmf);
            }

            obj_ptr get_object() const noexcept override {
                return get_object_ptr(ptr);
            }

#ifdef SIGSLOT_RTTI_ENABLED
            const std::type_info& get_callable_type() const noexcept override {
                return typeid(pmf);
            }
#endif

        private:
            std::decay_t<Ptr> ptr;
            std::decay_t<Pmf> pmf;
        };

        /*
         * An implementation of a slot that tracks the life of a supplied object
         * through a weak pointer in order to automatically disconnect the slot
         * on said object destruction.
         */
        template <typename WeakPtr, typename Func, typename... Args>
        class slot_tracked final : public slot_base<Args...>, public std::enable_shared_from_this<slot_tracked<WeakPtr, Func, Args...>> {
        public:
            using this_type = slot_tracked<Func, WeakPtr, Args...>;
            template <typename P, typename F>
            constexpr slot_tracked(cleanable& c, P&& p, F&& f, uint32_t type, core::TaskQueue* queue, group_id gid)
            : slot_base<Args...>(c, type, queue, gid)
            , ptr{std::forward<P>(p)}
            , func{std::forward<F>(f)} {}

            bool connected() const noexcept override {
                return !ptr.expired() && slot_state::connected();
            }

        protected:
            void call_slot(Args ...args) override {
                auto sp = ptr.lock();
                if (!sp) {
                    slot_state::disconnect();
                    return;
                }
                if (!this->can_emit()) {
                    return;
                }
                this->set_emitted();
                uint32_t type = this->type();
                if (type == connection_type::direct_connection) {
                    if (this->slot_state::connected()) {
                        func(args...);
                        if (this->m_singleshot && this->m_emitted) {
                            this->slot_state::disconnect();
                        }
                    } else {
                        std::cerr << "canceling slot execution due to connection being disconnected" << std::endl;
                    }
                } else if (type == connection_type::queued_connection) {
                    assert(this->m_queue);
                    if (this->m_queue) {
                        this->m_queue->PostTask([wself = std::weak_ptr<this_type>(this_type::shared_from_this()), args...]() mutable {
                            auto self = wself.lock();
                            if (!self) {
                                return;
                            }
                            if (self->slot_state::connected()) {
                                self->func(args...);
                                if (self->m_singleshot && self->m_emitted) {
                                    self->slot_state::disconnect();
                                }
                            } else {
                                std::cerr << "canceling slot execution due to connection being disconnected" << std::endl;
                            }
                        });
                    } else {
                        std::cerr << "thread is nullptr" << std::endl;
                    }
                } else if (type == connection_type::blocking_queued_connection) {
                    auto promise = std::promise<void>();
                    assert(this->m_queue);
                    this->m_queue->PostTask([this, &args..., &promise]() mutable {
                        if (this->slot_state::connected()) {
                            this->func(args...);
                            if (this->m_singleshot && this->m_emitted) {
                                this->slot_state::disconnect();
                            }
                        } else {
                            std::cerr << "canceling slot execution due to connection being disconnected" << std::endl;
                        }
                        promise.set_value();
                    });
                    promise.get_future().get();
                } else {
                    std::cerr << "illegal connection type" << std::endl;
                }
            }

            func_ptr get_callable() const noexcept override {
                return get_function_ptr(func);
            }

            obj_ptr get_object() const noexcept override {
                return get_object_ptr(ptr);
            }

#ifdef SIGSLOT_RTTI_ENABLED
            const std::type_info& get_callable_type() const noexcept override {
                return typeid(func);
            }
#endif

        private:
            std::decay_t<WeakPtr> ptr;
            std::decay_t<Func> func;
        };

        /*
         * An implementation of a slot as a pointer over member function, that tracks
         * the life of a supplied object through a weak pointer in order to automatically
         * disconnect the slot on said object destruction.
         */
        template <typename WeakPtr, typename Pmf, typename... Args>
        class slot_pmf_tracked final : public slot_base<Args...>, public std::enable_shared_from_this<slot_pmf_tracked<WeakPtr, Pmf, Args...>> {
        public:
            using this_type = slot_pmf_tracked<Pmf, WeakPtr, Args...>;
            template <typename P, typename F>
            constexpr slot_pmf_tracked(cleanable& c, P&& p, F&& f, uint32_t type, core::TaskQueue* queue, group_id gid)
            : slot_base<Args...>(c, type, queue, gid)
            , ptr{std::forward<P>(p)}
            , pmf{std::forward<F>(f)} {}

            bool connected() const noexcept override {
                return !ptr.expired() && slot_state::connected();
            }

        protected:
            void call_slot(Args ...args) override {
                auto sp = ptr.lock();
                if (!sp) {
                    slot_state::disconnect();
                    return;
                }
                if (!this->can_emit()) {
                    return;
                }
                this->set_emitted();
                uint32_t type = this->type();
                if (type == connection_type::direct_connection) {
                    if (this->slot_state::connected()) {
                        ((*sp).*pmf)(args...);
                        if (this->m_singleshot && this->m_emitted) {
                            this->slot_state::disconnect();
                        }
                    } else {
                        std::cerr << "canceling slot execution due to connection being disconnected" << std::endl;
                    }
                } else if (type == connection_type::queued_connection) {
                    assert(this->m_queue);
                    if (this->m_queue) {
                        this->m_queue->PostTask([wself = std::weak_ptr<this_type>(this_type::shared_from_this()), sp, args...]() mutable {
                            auto self = wself.lock();
                            if (!self) {
                                return;
                            }
                            if (self->slot_state::connected()) {
                                ((*sp).*(self->pmf))(args...);
                                if (self->m_singleshot && self->m_emitted) {
                                    self->slot_state::disconnect();
                                }
                            } else {
                                std::cerr << "canceling slot execution due to connection being disconnected" << std::endl;
                            }
                        });
                    } else {
                        std::cerr << "thread is nullptr" << std::endl;
                    }
                } else if (type == connection_type::blocking_queued_connection) {
                    auto promise = std::promise<void>();
                    assert(this->m_queue);
                    this->m_queue->PostTask([this, sp, &args..., &promise]() mutable {
                        if (this->slot_state::connected()) {
                            (*sp.*(this->pmf))(this->conn, args...);
                            if (this->m_singleshot && this->m_emitted) {
                                this->slot_state::disconnect();
                            }
                        } else {
                            std::cerr << "canceling slot execution due to connection being disconnected" << std::endl;
                        }
                        promise.set_value();
                    });
                    promise.get_future().get();
                } else {
                    std::cerr << "illegal connection type" << std::endl;
                }
            }

            func_ptr get_callable() const noexcept override {
                return get_function_ptr(pmf);
            }

            obj_ptr get_object() const noexcept override {
                return get_object_ptr(ptr);
            }

#ifdef SIGSLOT_RTTI_ENABLED
            const std::type_info& get_callable_type() const noexcept override {
                return typeid(pmf);
            }
#endif

        private:
            std::decay_t<WeakPtr> ptr;
            std::decay_t<Pmf> pmf;
        };

    } // namespace detail


    /**
     * signal_base is an implementation of the observer pattern, through the use
     * of an emitting object and slots that are connected to the signal and called
     * with supplied arguments when a signal is emitted.
     *      * signal_base is the general implementation, whose locking policy must be
     * set in order to decide thread safety guarantees. signal and signal_st
     * are partial specializations for multi-threaded and single-threaded use.
     *      * It does not allow slots to return a value.
     *      * Slot execution order can be constrained by assigning group ids to the slots.
     * The execution order of slots in a same group is unspecified and should not be
     * relied upon, however groups are executed in ascending group ids order. When
     * the group id of a slot is not set, it is assigned to the group 0. Group ids
     * can have any value in the range of signed 32 bit integers.
     *      * @tparam Lockable a lock type to decide the lock policy
     * @tparam T... the argument types of the emitting and slots functions.
     */
    template <typename Lockable, typename... T>
    class signal_base final : public detail::cleanable {
        template <typename L>
        using is_thread_safe = std::integral_constant<bool, !std::is_same<L, detail::null_mutex>::value>;

        template <typename U, typename L>
        using cow_type = std::conditional_t<is_thread_safe<L>::value,
                                            detail::copy_on_write<U>, U>;

        template <typename U, typename L>
        using cow_copy_type = std::conditional_t<is_thread_safe<L>::value,
                                                 detail::copy_on_write<U>, const U&>;

        using lock_type = std::unique_lock<Lockable>;
        using slot_base = detail::slot_base<T...>;
        using slot_ptr = detail::slot_ptr<T...>;
        using slots_type = std::vector<slot_ptr>;
        struct group_type { slots_type slts; group_id gid; };
        using list_type = std::vector<group_type>;  // kept ordered by ascending gid

    public:
        using arg_list = trait::typelist<T...>;
        using ext_arg_list = trait::typelist<connection&, T...>;

        signal_base() noexcept : m_block(false) {}
        ~signal_base() override {
            disconnect_all();
        }

        signal_base(const signal_base&) = delete;
        signal_base& operator=(const signal_base&) = delete;

        signal_base(signal_base&& o) /* not noexcept */
        : m_block{o.m_block.load()}
        {
            lock_type lock(o.m_mutex);
            using std::swap;
            swap(m_slots, o.m_slots);
        }

        signal_base& operator=(signal_base&& o) /* not noexcept */ {
            lock_type lock1(m_mutex, std::defer_lock);
            lock_type lock2(o.m_mutex, std::defer_lock);
            std::lock(lock1, lock2);

            using std::swap;
            swap(m_slots, o.m_slots);
            m_block.store(o.m_block.exchange(m_block.load()));
            return *this;
        }

        /**
         * Emit a signal
         *          * Effect: All non blocked and connected slot functions will be called
         *         with supplied arguments.
         * Safety: With proper locking (see pal::signal), emission can happen from
         *         multiple threads simultaneously. The guarantees only apply to the
         *         signal object, it does not cover thread safety of potentially
         *         shared state used in slot functions.
         *          * @param a... arguments to emit
         */
        template <typename... U>
        void operator()(U&& ...a) const {
            if (m_block) {
                return;
            }

            // Reference to the slots to execute them out of the lock
           // a copy may occur if another thread writes to it.
            cow_copy_type<list_type, Lockable> ref = slots_reference();

            for (const auto& group : detail::cow_read(ref)) {
                for (const auto& s : group.slts) {
                    s->operator()(a...);
                }
            }
        }

        /**
         * Connect a callable of compatible arguments
         *          * Effect: Creates and stores a new slot responsible for executing the
         *         supplied callable for every subsequent signal emission.
         * Safety: Thread-safety depends on locking policy.
         *          * @param c a callable
         * @param gid an identifier that can be used to order slot execution
         * @return a connection object that can be used to interact with the slot
         */
        template <typename Callable>
        std::enable_if_t<trait::is_callable_v<arg_list, Callable>, connection>
        connect(Callable&& c, uint32_t type = connection_type::direct_connection, core::TaskQueue* queue = nullptr, group_id gid = 0) {
            using slot_t = detail::slot<Callable, T...>;
            auto s = make_slot<slot_t>(std::forward<Callable>(c), type, queue, gid);
            auto o = get_slot([&](const auto& slot) {
                return slot->has_callable(c);
            });
            if (o && o->is_unique()) {
                connection conn;
                conn.disconnect();
                return conn;
            }
            if (type & connection_type::unique_connection) {
                s->set_unique(true);
            }
            connection conn(s);
            add_slot(std::move(s));
            return conn;
        }

        /**
         * Connect a callable with an additional connection argument
         *          * The callable's first argument must be of type connection. This overload
         * the callable to manage it's own connection through this argument.
         *          * @param c a callable
         * @param gid an identifier that can be used to order slot execution
         * @return a connection object that can be used to interact with the slot
         */
        template <typename Callable>
        std::enable_if_t<trait::is_callable_v<ext_arg_list, Callable>, connection>
        connect_extended(Callable&& c, uint32_t type = connection_type::direct_connection, core::TaskQueue* queue = nullptr, group_id gid = 0) {
            using slot_t = detail::slot_extended<Callable, T...>;
            auto s = make_slot<slot_t>(std::forward<Callable>(c), type, queue, gid);
            auto o = get_slot([&](const auto& slot) {
                return slot->has_callable(c);
            });
            if (o && o->is_unique()) {
                connection conn;
                conn.disconnect();
                return conn;
            }
            if (type & connection_type::unique_connection) {
                s->set_unique(true);
            }
            connection conn(s);
            std::static_pointer_cast<slot_t>(s)->conn = conn;
            add_slot(std::move(s));
            return conn;
        }

        /**
         * Overload of connect for pointers over member functions derived from
         * observer
         *          * @param pmf a pointer over member function
         * @param ptr an object pointer derived from observer
         * @param gid an identifier that can be used to order slot execution
         * @return a connection object that can be used to interact with the slot
         */
        template <typename Ptr, typename Pmf>
        std::enable_if_t<trait::is_callable_v<arg_list, Ptr, Pmf> &&
                             trait::is_observer_v<Ptr>, connection>
        connect(Ptr&& ptr, Pmf&& pmf, uint32_t type = connection_type::direct_connection, core::TaskQueue* queue = nullptr, group_id gid = 0) {
            using slot_t = detail::slot_pmf<Ptr, Pmf, T...>;
            auto s = make_slot<slot_t>(std::forward<Ptr>(ptr), std::forward<Pmf>(pmf), type, queue, gid);
            auto o = get_slot([&](const auto& slot) {
                return slot->has_object(ptr) && slot->has_callable(pmf);
            });
            if (o && o->is_unique()) {
                connection conn;
                conn.disconnect();
                return conn;
            }
            if (type & connection_type::unique_connection) {
                s->set_unique(true);
            }
            connection conn(s);
            add_slot(std::move(s));
            ptr->add_connection(conn);
            return conn;
        }

        /**
         * Overload of connect for pointers over member functions
         *          * @param pmf a pointer over member function
         * @param ptr an object pointer
         * @param gid an identifier that can be used to order slot execution
         * @return a connection object that can be used to interact with the slot
         */
        template <typename Ptr, typename Pmf>
        std::enable_if_t<trait::is_callable_v<arg_list, Ptr, Pmf> &&
                             !trait::is_observer_v<Ptr> &&
                             !trait::is_weak_ptr_compatible_v<Ptr>, connection>
        connect(Ptr&& ptr, Pmf&& pmf, uint32_t type = connection_type::direct_connection, core::TaskQueue* queue = nullptr, group_id gid = 0) {
            using slot_t = detail::slot_pmf<Ptr, Pmf, T...>;
            auto s = make_slot<slot_t>(std::forward<Ptr>(ptr), std::forward<Pmf>(pmf), type, queue, gid);
            auto o = get_slot([&](const auto& slot) {
                return slot->has_object(ptr) && slot->has_callable(pmf);
            });
            if (o && o->is_unique()) {
                connection conn;
                conn.disconnect();
                return conn;
            }
            if (type & connection_type::unique_connection) {
                s->set_unique(true);
            }
            connection conn(s);
            add_slot(std::move(s));
            return conn;
        }

        /**
         * Overload  of connect for pointer over member functions and
         *          * @param pmf a pointer over member function
         * @param ptr an object pointer
         * @param gid an identifier that can be used to order slot execution
         * @return a connection object that can be used to interact with the slot
         */
        template <typename Ptr, typename Pmf>
        std::enable_if_t<trait::is_callable_v<ext_arg_list, Ptr, Pmf> &&
                             !trait::is_weak_ptr_compatible_v<Ptr>, connection>
        connect_extended(Pmf&& pmf, Ptr&& ptr, uint32_t type = connection_type::direct_connection, core::TaskQueue* queue = nullptr, group_id gid = 0) {
            using slot_t = detail::slot_pmf_extended<Pmf, Ptr, T...>;
            auto s = make_slot<slot_t>(std::forward<Ptr>(ptr), std::forward<Pmf>(pmf), type, queue, gid);
            auto o = get_slot([&](const auto& slot) {
                return slot->has_object(ptr) && slot->has_callable(pmf);
            });
            if (o && o->is_unique()) {
                connection conn;
                conn.disconnect();
                return conn;
            }
            if (type & connection_type::unique_connection) {
                s->set_unique(true);
            }
            connection conn(s);
            std::static_pointer_cast<slot_t>(s)->conn = conn;
            add_slot(std::move(s));
            return conn;
        }

        /**
         * Overload of connect for lifetime object tracking and automatic disconnection
         *          * Ptr must be convertible to an object following a loose form of weak pointer
         * concept, by implementing the ADL-detected conversion function to_weak().
         *          * This overload covers the case of a pointer over member function and a
         * trackable pointer of that class.
         *          * Note: only weak references are stored, a slot does not extend the lifetime
         * of a suppied object.
         *          * @param pmf a pointer over member function
         * @param ptr a trackable object pointer
         * @param gid an identifier that can be used to order slot execution
         * @return a connection object that can be used to interact with the slot
         */
        template <typename Ptr, typename Pmf>
        std::enable_if_t<!trait::is_callable_v<arg_list, Pmf> &&
                             trait::is_weak_ptr_compatible_v<Ptr>, connection>
        connect(Ptr&& ptr, Pmf&& pmf, uint32_t type = connection_type::direct_connection, core::TaskQueue* queue = nullptr, group_id gid = 0) {
            using trait::to_weak;
            auto w = to_weak(std::forward<Ptr>(ptr));
            using slot_t = detail::slot_pmf_tracked<Pmf, decltype(w), T...>;
            auto s = make_slot<slot_t>(w, std::forward<Pmf>(pmf), type, queue, gid);
            auto o = get_slot([&](const auto& slot) {
                return slot->has_object(ptr) && slot->has_callable(pmf);
            });
            if (o && o->is_unique()) {
                connection conn;
                conn.disconnect();
                return conn;
            }
            if (type & connection_type::unique_connection) {
                s->set_unique(true);
            }
            connection conn(s);
            add_slot(std::move(s));
            return conn;
        }

        /**
         * Overload of connect for lifetime object tracking and automatic disconnection
         *          * Trackable must be convertible to an object following a loose form of weak
         * pointer concept, by implementing the ADL-detected conversion function to_weak().
         *          * This overload covers the case of a standalone callable and unrelated trackable
         * object.
         *          * Note: only weak references are stored, a slot does not extend the lifetime
         * of a suppied object.
         *          * @param c a callable
         * @param ptr a trackable object pointer
         * @param gid an identifier that can be used to order slot execution
         * @return a connection object that can be used to interact with the slot
         */
        template <typename Trackable, typename Callable>
        std::enable_if_t<trait::is_callable_v<arg_list, Callable> &&
                             trait::is_weak_ptr_compatible_v<Trackable>, connection>
        connect(Trackable&& ptr, Callable&& c, uint32_t type = connection_type::direct_connection, core::TaskQueue* queue = nullptr, group_id gid = 0) {
            using trait::to_weak;
            auto w = to_weak(std::forward<Trackable>(ptr));
            using slot_t = detail::slot_tracked<Callable, decltype(w), T...>;
            auto s = make_slot<slot_t>(w, std::forward<Callable>(c), type, queue, gid);
            auto o = get_slot([&](const auto& slot) {
                return slot->has_callable(c);
            });
            if (o && o->is_unique()) {
                connection conn;
                conn.disconnect();
                return conn;
            }
            if (type & connection_type::unique_connection) {
                s->set_unique(true);
            }
            connection conn(s);
            add_slot(std::move(s));
            return conn;
        }

        /**
         * Creates a connection whose duration is tied to the return object
         * Use the same semantics as connect
         */
        template <typename... CallArgs>
        scoped_connection connect_scoped(CallArgs&& ...args) {
            return connect(std::forward<CallArgs>(args)...);
        }

        /**
         * Disconnect slots bound to a callable
         *          * Effect: Disconnects all the slots bound to the callable in argument.
         * Safety: Thread-safety depends on locking policy.
         *          * If the callable is a free or static member function, this overload is always
         * available. However, RTTI is needed for it to work for pointer to member
         * functions, function objects or and (references to) lambdas, because the
         * C++ spec does not mandate the pointers to member functions to be unique.
         *          * @param c a callable
         * @return the number of disconnected slots
         */
        template <typename Callable>
        std::enable_if_t<(trait::is_callable_v<arg_list, Callable> ||
                          trait::is_callable_v<ext_arg_list, Callable> ||
                          trait::is_pmf_v<Callable>) &&
                         detail::function_traits<Callable>::is_disconnectable, size_t>
        disconnect(const Callable& c) {
            return disconnect_if([&] (const auto& s) {
                return s->has_full_callable(c);
            });
        }

        /**
         * Disconnect slots bound to this object
         *          * Effect: Disconnects all the slots bound to the object or tracked object
         *         in argument.
         * Safety: Thread-safety depends on locking policy.
         *          * The object may be a pointer or trackable object.
         *          * @param obj an object
         * @return the number of disconnected slots
         */
        template <typename Obj>
        std::enable_if_t<!trait::is_callable_v<arg_list, Obj> &&
                             !trait::is_callable_v<ext_arg_list, Obj> &&
                             !trait::is_pmf_v<Obj>, size_t>
        disconnect(const Obj& obj) {
            return disconnect_if([&] (const auto& s) {
                return s->has_object(obj);
            });
        }

        /**
         * Disconnect slots bound both to a callable and object
         *          * Effect: Disconnects all the slots bound to the callable and object in argument.
         * Safety: Thread-safety depends on locking policy.
         *          * For naked pointers, the Callable is expected to be a pointer over member
         * function. If obj is trackable, any kind of Callable can be used.
         *          * @param c a callable
         * @param obj an object
         * @return the number of disconnected slots
         */
        template <typename Obj, typename Callable>
        size_t disconnect(const Obj& obj, const Callable& c) {
            return disconnect_if([&] (const auto& s) {
                return s->has_object(obj) && s->has_callable(c);
            });
        }

        /**
         * Disconnect slots in a particular group
         *          * Effect: Disconnects all the slots in the group id in argument.
         * Safety: Thread-safety depends on locking policy.
         *          * @param gid a group id
         * @return the number of disconnected slots
         */
        size_t disconnect(group_id gid) {
            lock_type lock(m_mutex);
            for (auto& group : detail::cow_write(m_slots)) {
                if (group.gid == gid) {
                    size_t count = group.slts.size();
                    group.slts.clear();
                    return count;
                }
            }
            return 0;
        }

        /**
         * Disconnects all the slots
         * Safety: Thread safety depends on locking policy
         */
        void disconnect_all() {
            lock_type lock(m_mutex);
            clear();
        }

        /**
         * Blocks signal emission
         * Safety: thread safe
         */
        void block() noexcept {
            m_block.store(true);
        }

        /**
         * Unblocks signal emission
         * Safety: thread safe
         */
        void unblock() noexcept {
            m_block.store(false);
        }

        /**
         * Tests blocking state of signal emission
         */
        bool blocked() const noexcept {
            return m_block.load();
        }

        /**
         * Get number of connected slots
         * Safety: thread safe
         */
        size_t slot_count() noexcept {
            cow_copy_type<list_type, Lockable> ref = slots_reference();
            size_t count = 0;
            for (const auto& g : detail::cow_read(ref)) {
                count += g.slts.size();
            }
            return count;
        }

    protected:
        /**
         * remove disconnected slots
         */
        void clean(detail::slot_state *state) override {
            lock_type lock(m_mutex);
            const auto idx = state->index();
            const auto gid = state->group();

            // find the group
            for (auto &group : detail::cow_write(m_slots)) {
                if (group.gid == gid) {
                    auto &slts = group.slts;

                    // ensure we have the right slot, in case of concurrent cleaning
                    if (idx < slts.size() && slts[idx] && slts[idx].get() == state) {
                        std::swap(slts[idx], slts.back());
                        slts[idx]->index() = idx;
                        slts.pop_back();
                    }

                    return;
                }
            }
        }

    private:
        // used to get a reference to the slots for reading
        inline cow_copy_type<list_type, Lockable> slots_reference() const {
            lock_type lock(m_mutex);
            return m_slots;
        }

        // create a new slot
        template <typename Slot, typename... A>
        inline auto make_slot(A&& ...a) {
            return detail::make_shared<slot_base, Slot>(*this, std::forward<A>(a)...);
        }

        // add the slot to the list of slots of the right group
        void add_slot(slot_ptr&& s) {
            const group_id gid = s->group();

            lock_type lock(m_mutex);
            auto &groups = detail::cow_write(m_slots);

            // find the group
            auto it = groups.begin();
            while (it != groups.end() && it->gid < gid) {
                it++;
            }

            // create a new group if necessary
            if (it == groups.end() || it->gid != gid) {
                it = groups.insert(it, {{}, gid});
            }

            // add the slot
            s->index() = it->slts.size();
            it->slts.push_back(std::move(s));
        }

        template <typename Cond>
        bool has_slot(Cond&& cond) {
            lock_type lock(m_mutex);
            auto& groups = detail::cow_write(m_slots);

            for (auto& group : groups) {
                auto& slts = group.slts;
                size_t i = 0;
                while (i < slts.size()) {
                    if (cond(slts[i])) {
                        return true;
                    } else {
                        ++i;
                    }
                }
            }
            return false;
        }

        template <typename Cond>
        slot_ptr get_slot(Cond&& cond) {
            lock_type lock(m_mutex);
            auto& groups = detail::cow_write(m_slots);

            for (auto& group : groups) {
                auto& slts = group.slts;
                size_t i = 0;
                while (i < slts.size()) {
                    if (cond(slts[i])) {
                        return slts[i];
                    } else {
                        ++i;
                    }
                }
            }
            return nullptr;
        }

        // disconnect a slot if a condition occurs
        template <typename Cond>
        size_t disconnect_if(Cond&& cond) {
            lock_type lock(m_mutex);
            auto& groups = detail::cow_write(m_slots);

            size_t count = 0;

            for (auto& group : groups) {
                auto& slts = group.slts;
                size_t i = 0;
                while (i < slts.size()) {
                    if (cond(slts[i])) {
                        std::swap(slts[i], slts.back());
                        slts[i]->index() = i;
                        slts.pop_back();
                        ++count;
                    } else {
                        ++i;
                    }
                }
            }

            return count;
        }

        // to be called under lock: remove all the slots
        void clear() {
            detail::cow_write(m_slots).clear();
        }

    private:
        mutable Lockable m_mutex;
        cow_type<list_type, Lockable> m_slots;
        std::atomic<bool> m_block;
    };


    /**
     * Freestanding connect function that defers to the `signal_base::connect` member.
     */
    template <typename Lockable, typename Arg, typename... T, typename ...Args>
    std::enable_if_t<!trait::is_signal_v<std::decay_t<Arg>>, connection>
    connect(signal_base<Lockable, T...>& sig, Arg&& arg, Args&& ...args)
    {
        return sig.connect(std::forward<Arg>(arg), std::forward<Args>(args)...);
    }

    /**
     * Freestanding connect function that chains one signal to another.
     */
    template <typename Lockable1, typename Lockable2, typename... T1, typename... T2, typename... Args>
    connection connect(signal_base<Lockable1, T1...>& sig1,
                       signal_base<Lockable2, T2...>& sig2,
                       Args&& ...args)
    {
        return sig1.connect(detail::signal_wrapper<signal_base<Lockable2, T2...>>{std::addressof(sig2)},
                            std::forward<Args>(args)...);
    }


    /**
     * Specialization of signal_base to be used in single threaded contexts.
     * Slot connection, disconnection and signal emission are not thread-safe.
     * The performance improvement over the thread-safe variant is not impressive,
     * so this is not very useful.
     */
    template <typename... T>
    using signal_st = signal_base<detail::null_mutex, T...>;

    /**
     * Specialization of signal_base to be used in multi-threaded contexts.
     * Slot connection, disconnection and signal emission are thread-safe.
     *      * Recursive signal emission and emission cycles are supported too.
     */
    template <typename... T>
    using signal = signal_base<std::mutex, T...>;

} // namespace sigslot

