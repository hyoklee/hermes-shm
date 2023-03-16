/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 * Distributed under BSD 3-Clause license.                                   *
 * Copyright by The HDF Group.                                               *
 * Copyright by the Illinois Institute of Technology.                        *
 * All rights reserved.                                                      *
 *                                                                           *
 * This file is part of Hermes. The full Hermes copyright notice, including  *
 * terms governing use, modification, and redistribution, is contained in    *
 * the COPYING file, which can be found at the top directory. If you do not  *
 * have access to the file, you may request a copy from help@hdfgroup.org.   *
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */


#ifndef HERMES_DATA_STRUCTURES_PTR_H_
#define HERMES_DATA_STRUCTURES_PTR_H_

#include "hermes_shm/constants/macros.h"
#include "hermes_shm/memory/memory.h"
#include "hermes_shm/memory/allocator/allocator.h"
#include "hermes_shm/memory/memory_registry.h"
#include "hermes_shm/data_structures/internal/shm_macros.h"
#include "hermes_shm/data_structures/internal/shm_archive.h"
#include "hermes_shm/data_structures/internal/shm_smart_ptr.h"

namespace hermes_shm::ipc {

/**
 * When T is a SHM object
 * */
template<typename T, bool destructable>
class _RefShm {
 public:
  typedef typename T::header_t header_t;
  char obj_[sizeof(T)];

 public:
  /**====================================
  * Initialization
  * ===================================*/

  /** SHM Constructor. */
  template<typename ...Args>
  void shm_init(Args&& ...args) {
    Allocator::ConstructObj<T>(
      *get(), std::forward<Args>(args)...);
  }

  /**====================================
  * Dereference Operations
  * ===================================*/

  /** Gets a pointer to the internal object */
  T* get() {
    return reinterpret_cast<T*>(obj_);
  }

  /** Gets a pointer to the internal object */
  const T* get() const {
    return reinterpret_cast<const T*>(obj_);
  }

  /**====================================
  * Move + Copy Operations
  * ===================================*/

  /** Internal copy operation */
  void shm_strong_copy(const _RefShm &other) {
    get()->shm_deserialize(other.get()->GetShmDeserialize());
  }

  /**====================================
  * Deserialization + Serialization
  * ===================================*/

  /** Deserialize from a ShmDeserialize. */
  void shm_deserialize(const ShmDeserialize<T> &ar) {
    get()->shm_deserialize(ar);
  }

  /** Deserialize from an object. */
  void shm_deserialize(T &ar) {
    get()->shm_deserialize(ar.GetShmDeserialize());
  }

  /** Serialize into a pointer type */
  template<typename PointerT>
  void shm_serialize(PointerT &ar) const {
    get()->shm_serialize(ar);
  }

  /**====================================
  * Destructor
  * ===================================*/

  /** Destroy the data allocated by this pointer */
  void shm_destroy() {
    if constexpr(destructable) {
      get()->SetHeaderOwned();
    } else {
      get()->UnsetHeaderOwned();
    }
    get()->shm_destroy();
  }
};

/**
 * When T is not a SHM object
 * */
template<typename T, bool destructable>
class _RefNoShm {
 public:
  T *obj_;
  Allocator *alloc_;

 public:
  /**====================================
  * Initialization
  * ===================================*/

  /** SHM constructor. Default allocator. */
  template<typename ...Args>
  void shm_init(Args&& ...args) {
    auto alloc = HERMES_MEMORY_REGISTRY->GetDefaultAllocator();
    shm_init(alloc, std::forward<Args>(args)...);
  }

  /** SHM constructor. Default non-default allocator */
  template<typename ...Args>
  void shm_init(Allocator *alloc, Args&& ...args) {
    alloc_ = alloc;
    OffsetPointer p;
    obj_ = alloc_->template AllocateConstructObjs<T, OffsetPointer>(
      1, p, std::forward<Args>(args)...);
  }

  /** SHM constructor. Header is preallocated. */
  template<typename ...Args>
  void shm_init(ShmArchive<T> &obj, Allocator *alloc, Args&& ...args) {
    alloc_ = alloc;
    obj_ = obj.get();
    alloc_->template ConstructObj<T>(
      *obj_, std::forward<Args>(args)...);
  }

  /**====================================
  * Dereference Operations
  * ===================================*/

  /** Gets a pointer to the internal object */
  T* get() {
    return obj_;
  }

  /** Gets a pointer to the internal object */
  const T* get() const {
    return obj_;
  }

  /**====================================
  * Move + Copy Operations
  * ===================================*/

  /** Internal copy operation */
  void shm_strong_copy(const _RefNoShm &other) {
    obj_ = other.obj_;
    alloc_ = other.alloc_;
  }

  /**====================================
  * Deserialization + Serialization
  * ===================================*/

  /** Deserialize from a ShmDeserialize. */
  void shm_deserialize(const ShmDeserialize<T> &ar) {
    obj_ = ar.header_;
    alloc_ = ar.alloc_;
  }

  /** Deserialize from an object. */
  void shm_deserialize(T &ar) {
    obj_ = &ar;
    // NOTE(llogan): alloc is not valid in this mode
  }

  /** Serialize into a pointer type */
  template<typename PointerT>
  void shm_serialize(PointerT &ar) const {
    ar.allocator_id_ = alloc_->GetId();
    ar.off_ = alloc_->template Convert<T, PointerT>(obj_);
  }

  /**====================================
  * Destructor
  * ===================================*/

  /** Destroy the data pointed by this ref */
  void shm_destroy() {
    if constexpr(destructable) {
      if (obj_ != nullptr) {
        alloc_->FreePtr<T>(obj_);
      }
    }
  }
};

/**
 * Decide which reference type to use
 * */
#define MAKE_REF_SHM_OR_NO_SHM(T, D) \
  SHM_X_OR_Y(T, (_RefShm<T, D>), (_RefNoShm<T, D>))

/**
 * MACROS to simplify the ptr namespace
 * */
#define CLASS_NAME smart_ptr_base
#define TYPED_CLASS smart_ptr_base<T>

/**
 * Creates a unique instance of a shared-memory data structure
 * and deletes eventually.
 * */
template<typename T, bool unique, bool destructable>
class smart_ptr_base {
 public:
  typedef MAKE_REF_SHM_OR_NO_SHM(T, destructable) T_Ref;
  T_Ref obj_;   /**< The stored shared-memory object */
  bool owner_;  /**< Whether the shared-memory object is owned */

 public:
  /**====================================
  * Initialization + Destruction
  * ===================================*/

  /** Default constructor does nothing */
  smart_ptr_base() = default;

  /** Create the mptr contents */
  template<typename ...Args>
  void shm_init(Args&& ...args) {
    obj_.shm_init(std::forward<Args>(args)...);
    if constexpr(unique) {
      owner_ = true;
    }
  }

  /** Destructor. Does not free data. */
  ~smart_ptr_base() {
    if constexpr(unique) {
      if (owner_) {
        shm_destroy();
      }
    }
  }

  /** Explicit destructor */
  void shm_destroy() {
    obj_.shm_destroy();
  }

  /**====================================
  * Dereference Operations
  * ===================================*/

  /** Gets a pointer to the internal object */
  T* get() {
    return obj_.get();
  }

  /** Gets a pointer to the internal object */
  const T* get() const {
    return obj_.get();
  }

  /** Dereference operator */
  T& operator*() {
    return *obj_.get();
  }

  /** Constant Dereference operator */
  const T& operator*() const {
    return *obj_.get();
  }

  /** Pointer operator */
  T* operator->() {
    return obj_.get();
  }

  /** Constant pointer operator */
  const T* operator->() const {
    return obj_.get();
  }

  /**====================================
  * Move + Copy Operations
  * ===================================*/

  /** Copy constructor (equivalent to move) */
  smart_ptr_base(const smart_ptr_base &other) {
    obj_.shm_strong_copy(other.obj_);
  };

  /** Copy assignment operator (equivalent to move) */
  smart_ptr_base& operator=(const smart_ptr_base &other) {
    if (this != &other) {
      shm_strong_copy(other);
    }
    return *this;
  }

  /** Move constructor */
  smart_ptr_base(smart_ptr_base&& other) noexcept {
    shm_strong_copy(other);
  }

  /** Move assignment operator */
  smart_ptr_base& operator=(smart_ptr_base&& other) noexcept {
    if (this != &other) {
      shm_strong_copy(other);
    }
    return *this;
  }

  /** Internal move operation */
  void shm_strong_copy(const smart_ptr_base &other) {
    obj_.shm_strong_copy(other.obj_);
    if constexpr(unique) {
      owner_ = other.owner_;
      // TODO(llogan): fix
      // other.owner_ = false;
    }
  }

  /**====================================
  * Deserialization
  * ===================================*/

  /** Constructor. Deserialize from a TypedPointer<T> */
  explicit smart_ptr_base(const TypedPointer<T> &ar) {
    shm_deserialize(ar);
  }

  /** Constructor. Deserialize from a TypedAtomicPointer<T> */
  explicit smart_ptr_base(const TypedAtomicPointer<T> &ar) {
    shm_deserialize(ar);
  }

  /** Constructor. Deserialize from an Archive. */
  explicit smart_ptr_base(ShmArchive<T> &ar, Allocator *alloc) {
    shm_deserialize(ShmDeserialize<T>(ar.get(), alloc));
  }

  /** Constructor. Deserialize from a ShmDeserialize. */
  explicit smart_ptr_base(const ShmDeserialize<T> &ar) {
    shm_deserialize(ar);
  }

  /** Constructor. Deserialize from an object. */
  explicit smart_ptr_base(T &ar) {
    shm_deserialize(ar);
  }

  /** Deserialize from a TypedPointer<T> */
  void shm_deserialize(const TypedPointer<T> &ar) {
    auto deserial = ShmDeserialize<T>(ar);
    shm_deserialize(deserial);
  }

  /** Deserialize from a TypedAtomicPointer<T> */
  void shm_deserialize(const TypedAtomicPointer<T> &ar) {
    auto deserial = ShmDeserialize<T>(ar);
    shm_deserialize(deserial);
  }

  /** Deserialize from a ShmDeserialize. */
  void shm_deserialize(const ShmDeserialize<T> &ar) {
    obj_.shm_deserialize(ar);
    if constexpr(unique) {
      owner_ = false;
    }
  }

  /** Deserialize from a reference to the object */
  void shm_deserialize(T &ar) {
    obj_.shm_deserialize(ar);
    if constexpr(unique) {
      owner_ = false;
    }
  }

  /**====================================
  * Serialization
  * ===================================*/

  /** Serialize to a TypedPointer<T> */
  void shm_serialize(TypedPointer<T> &ar) const {
    obj_.template shm_serialize<TypedPointer<T>>(ar);
  }

  /** Serialize to a TypedAtomicPointer<T> */
  void shm_serialize(TypedAtomicPointer<T> &ar) const {
    obj_.template shm_serialize<TypedAtomicPointer<T>>(ar);
  }

  /**====================================
  * Serialization + Deserialization Ops
  * ===================================*/
  SHM_SERIALIZE_DESERIALIZE_OPS((T))

  /**====================================
  * Hash function
  * ===================================*/
  size_t hash() const {
    return std::hash<T>{}(*this);
  }
};

/**====================================
* Helper Functions
* ===================================*/

/** Mptr is non-unique and requires explicit destruction */
template<typename T>
using mptr = smart_ptr_base<T, false, true>;

/** Refs are like mptr, but don't have a destructable header */
template<typename T>
using Ref = smart_ptr_base<T, false, false>;

/** Uptr has a specific container owning the object */
template<typename T>
using uptr = smart_ptr_base<T, true, true>;

/** Construct an mptr with default allocator */
template<typename PointerT, typename ...Args>
static PointerT make_ptr_base(Args&& ...args) {
  PointerT ptr;
  ptr.shm_init(std::forward<Args>(args)...);
  return ptr;
}

/** Creates a smart_ptr by merging two argpacks */
template<typename PointerT, typename ArgPackT_1, typename ArgPackT_2>
static PointerT make_piecewise(ArgPackT_1 &&args1, ArgPackT_2 &&args2) {
  return hermes_shm::PassArgPack::Call(
    MergeArgPacks::Merge(
      std::forward<ArgPackT_1>(args1),
      std::forward<ArgPackT_2>(args2)),
    [](auto&& ...args) constexpr {
      return make_ptr_base<PointerT>(std::forward<decltype(args)>(args)...);
    });
}

/** Creates a reference to an already-allocated object */
template<typename T, typename ...Args>
Ref<T> make_ref(Args&& ...args) {
  return make_ptr_base<Ref<T>>(std::forward<Args>(args)...);
}

/** Creates a reference from piecewise constructor */
template<typename T, typename ArgPackT_1, typename ArgPackT_2>
Ref<T> make_ref_piecewise(ArgPackT_1 &&args1, ArgPackT_2 &&args2) {
  return make_piecewise<Ref<T>, ArgPackT_1, ArgPackT_2>(
    std::forward<ArgPackT_1>(args1),
    std::forward<ArgPackT_2>(args2)
  );
}

/** Create a manual pointer with default allocator */
template<typename T, typename ...Args>
mptr<T> make_mptr(Args&& ...args) {
  return make_ptr_base<mptr<T>>(std::forward<Args>(args)...);
}

/** Create a unique pointer with default allocator */
template<typename T, typename ...Args>
uptr<T> make_uptr(Args&& ...args) {
  return make_ptr_base<uptr<T>>(std::forward<Args>(args)...);
}

}  // namespace hermes_shm::ipc

#undef CLASS_NAME
#undef TYPED_CLASS

/**====================================
* Hash Functions
* ===================================*/
namespace std {

/** Hash function for mptr */
template<typename T>
struct hash<hermes_shm::ipc::mptr<T>> {
  size_t operator()(const hermes_shm::ipc::mptr<T> &obj) const {
    return obj.hash();
  }
};

/** Hash function for uptr */
template<typename T>
struct hash<hermes_shm::ipc::uptr<T>> {
  size_t operator()(const hermes_shm::ipc::uptr<T> &obj) const {
    return obj.hash();
  }
};

}  // namespace std

#endif  // HERMES_DATA_STRUCTURES_PTR_H_