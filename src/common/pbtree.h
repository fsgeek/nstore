#ifndef PBTREE_H_
#define PBTREE_H_

#include <algorithm>
#include <functional>
#include <istream>
#include <ostream>
#include <memory>
#include <cstddef>
#include <assert.h>

#include "libpm.h"

/// Print out debug information to std::cout if BTREE_DEBUG is defined.
#define BTREE_PRINT(x)

/// Assertion only if BTREE_DEBUG is defined. This is not used in verify().
#define BTREE_ASSERT(x)

/// The maximum of a and b. Used in some compile-time formulas.
#define BTREE_MAX(a,b)          ((a) < (b) ? (b) : (a))

#ifndef BTREE_FRIENDS
/// The macro BTREE_FRIENDS can be used by outside class to access the B+
/// tree internals. This was added for wxBTreeDemo to be able to draw the
/// tree.
#define BTREE_FRIENDS           friend class btree_friend;
#endif

/** Generates default traits for a B+ tree used as a set. It estimates leaf and
 * inner node sizes by assuming a cache line size of 256 bytes. */
template<typename _Key>
struct btree_default_set_traits {
  /// If true, the tree will self verify it's invariants after each insert()
  /// or erase(). The header must have been compiled with BTREE_DEBUG defined.
  static const bool selfverify = false;

  /// If true, the tree will print out debug information and a tree dump
  /// during insert() or erase() operation. The header must have been
  /// compiled with BTREE_DEBUG defined and key_type must be std::ostream
  /// printable.
  static const bool debug = false;

  /// Number of slots in each leaf of the tree. Estimated so that each node
  /// has a size of about 256 bytes.
  static const int leafslots = BTREE_MAX(8, 256 / (sizeof(_Key)));

  /// Number of slots in each inner node of the tree. Estimated so that each node
  /// has a size of about 256 bytes.
  static const int innerslots = BTREE_MAX(8,
                                          256 / (sizeof(_Key) + sizeof(void*)));

  /// As of stx-btree-0.9, the code does linear search in find_lower() and
  /// find_upper() instead of binary_search, unless the node size is larger
  /// than this threshold. See notes at
  /// http://panthema.net/2013/0504-STX-B+Tree-Binary-vs-Linear-Search
  static const size_t binsearch_threshold = 256;
};

/** Generates default traits for a B+ tree used as a map. It estimates leaf and
 * inner node sizes by assuming a cache line size of 256 bytes. */
template<typename _Key, typename _Data>
struct btree_default_map_traits {
  /// If true, the tree will self verify it's invariants after each insert()
  /// or erase(). The header must have been compiled with BTREE_DEBUG defined.
  static const bool selfverify = false;

  /// If true, the tree will print out debug information and a tree dump
  /// during insert() or erase() operation. The header must have been
  /// compiled with BTREE_DEBUG defined and key_type must be std::ostream
  /// printable.
  static const bool debug = false;

  /// Number of slots in each leaf of the tree. Estimated so that each node
  /// has a size of about 256 bytes.
  static const int leafslots = BTREE_MAX(8,
                                         256 / (sizeof(_Key) + sizeof(_Data)));

  /// Number of slots in each inner node of the tree. Estimated so that each node
  /// has a size of about 256 bytes.
  static const int innerslots = BTREE_MAX(8,
                                          256 / (sizeof(_Key) + sizeof(void*)));

  /// As of stx-btree-0.9, the code does linear search in find_lower() and
  /// find_upper() instead of binary_search, unless the node size is larger
  /// than this threshold. See notes at
  /// http://panthema.net/2013/0504-STX-B+Tree-Binary-vs-Linear-Search
  static const size_t binsearch_threshold = 256;
};

/** @brief Basic class implementing a base B+ tree data structure in memory.
 *
 * The base implementation of a memory B+ tree. It is based on the
 * implementation in Cormen's Introduction into Algorithms, Jan Jannink's paper
 * and other algorithm resources. Almost all STL-required function calls are
 * implemented. The asymptotic time requirements of the STL are not always
 * fulfilled in theory, however in practice this B+ tree performs better than a
 * red-black tree by using more memory. The insertion function splits the nodes
 * on the recursion unroll. Erase is largely based on Jannink's ideas.
 *
 * This class is specialized into btree_set, btree_multiset, btree_map and
 * btree_multimap using default template parameters and facade functions.
 */
template<typename _Key, typename _Data,
    typename _Value = std::pair<_Key, _Data>,
    typename _Compare = std::less<_Key>,
    typename _Traits = btree_default_map_traits<_Key, _Data>, bool _Duplicates =
        false, typename _Alloc = std::allocator<_Value>, bool _UsedAsSet = false>
class btree {
 public:
  // *** Template Parameter Types

  /// First template parameter: The key type of the B+ tree. This is stored
  /// in inner nodes and leaves
  typedef _Key key_type;

  /// Second template parameter: The data type associated with each
  /// key. Stored in the B+ tree's leaves
  typedef _Data data_type;

  /// Third template parameter: Composition pair of key and data types, this
  /// is required by the STL standard. The B+ tree does not store key and
  /// data together. If value_type == key_type then the B+ tree implements a
  /// set.
  typedef _Value value_type;

  /// Fourth template parameter: Key comparison function object
  typedef _Compare key_compare;

  /// Fifth template parameter: Traits object used to define more parameters
  /// of the B+ tree
  typedef _Traits traits;

  /// Sixth template parameter: Allow duplicate keys in the B+ tree. Used to
  /// implement multiset and multimap.
  static const bool allow_duplicates = _Duplicates;

  /// Seventh template parameter: STL allocator for tree nodes
  typedef _Alloc allocator_type;

  /// Eighth template parameter: boolean indicator whether the btree is used
  /// as a set. In this case all operations on the data arrays are
  /// omitted. This flag is kind of hacky, but required because
  /// sizeof(empty_struct) = 1 due to the C standard. Without the flag, lots
  /// of superfluous copying would occur.
  static const bool used_as_set = _UsedAsSet;

  // The macro BTREE_FRIENDS can be used by outside class to access the B+
  // tree internals. This was added for wxBTreeDemo to be able to draw the
  // tree.
  BTREE_FRIENDS

 public:
  // *** Constructed Types

  /// Typedef of our own type
  typedef btree<key_type, data_type, value_type, key_compare, traits,
      allow_duplicates, allocator_type, used_as_set> btree_self;

  /// Size type used to count keys
  typedef size_t size_type;

  /// The pair of key_type and data_type, this may be different from value_type.
  typedef std::pair<key_type, data_type> pair_type;

 public:
  // *** Static Constant Options and Values of the B+ Tree

  /// Base B+ tree parameter: The number of key/data slots in each leaf
  static const unsigned short leafslotmax = traits::leafslots;

  /// Base B+ tree parameter: The number of key slots in each inner node,
  /// this can differ from slots in each leaf.
  static const unsigned short innerslotmax = traits::innerslots;

  /// Computed B+ tree parameter: The minimum number of key/data slots used
  /// in a leaf. If fewer slots are used, the leaf will be merged or slots
  /// shifted from it's siblings.
  static const unsigned short minleafslots = (leafslotmax / 2);

  /// Computed B+ tree parameter: The minimum number of key slots used
  /// in an inner node. If fewer slots are used, the inner node will be
  /// merged or slots shifted from it's siblings.
  static const unsigned short mininnerslots = (innerslotmax / 2);

  /// Debug parameter: Enables expensive and thorough checking of the B+ tree
  /// invariants after each insert/erase operation.
  static const bool selfverify = traits::selfverify;

  /// Debug parameter: Prints out lots of debug information about how the
  /// algorithms change the tree. Requires the header file to be compiled
  /// with BTREE_DEBUG and the key type must be std::ostream printable.
  static const bool debug = traits::debug;

 private:
  // *** Node Classes for In-Memory Nodes

  /// The header structure of each node in-memory. This structure is extended
  /// by inner_node or leaf_node.
  struct node {
    /// Level in the b-tree, if level == 0 -> leaf node
    unsigned short level;

    /// Number of key slotuse use, so number of valid children or data
    /// pointers
    unsigned short slotuse;

    /// Delayed initialisation of constructed node
    inline void initialize(const unsigned short l) {
      level = l;
      slotuse = 0;
    }

    /// True if this is a leaf node
    inline bool isleafnode() const {
      return (level == 0);
    }
  };

  /// Extended structure of a inner node in-memory. Contains only keys and no
  /// data items.
  struct inner_node : public node {
    /// Define an related allocator for the inner_node structs.
    typedef typename _Alloc::template rebind<inner_node>::other alloc_type;

    /// Keys of children or data pointers
    key_type slotkey[innerslotmax];

    /// Pointers to children
    node* childid[innerslotmax + 1];

    /// Set variables to initial values
    inline void initialize(const unsigned short l) {
      node::initialize(l);
    }

    /// True if the node's slots are full
    inline bool isfull() const {
      return (node::slotuse == innerslotmax);
    }

    /// True if few used entries, less than half full
    inline bool isfew() const {
      return (node::slotuse <= mininnerslots);
    }

    /// True if node has too few entries
    inline bool isunderflow() const {
      return (node::slotuse < mininnerslots);
    }
  };

  /// Extended structure of a leaf node in memory. Contains pairs of keys and
  /// data items. Key and data slots are kept in separate arrays, because the
  /// key array is traversed very often compared to accessing the data items.
  struct leaf_node : public node {
    /// Define an related allocator for the leaf_node structs.
    typedef typename _Alloc::template rebind<leaf_node>::other alloc_type;

    /// Double linked list pointers to traverse the leaves
    leaf_node *prevleaf;

    /// Double linked list pointers to traverse the leaves
    leaf_node *nextleaf;

    /// Keys of children or data pointers
    key_type slotkey[leafslotmax];

    /// Array of data
    data_type slotdata[used_as_set ? 1 : leafslotmax];

    /// Set variables to initial values
    inline void initialize() {
      node::initialize(0);
      prevleaf = nextleaf = NULL;
    }

    /// True if the node's slots are full
    inline bool isfull() const {
      return (node::slotuse == leafslotmax);
    }

    /// True if few used entries, less than half full
    inline bool isfew() const {
      return (node::slotuse <= minleafslots);
    }

    /// True if node has too few entries
    inline bool isunderflow() const {
      return (node::slotuse < minleafslots);
    }

    /// Set the (key,data) pair in slot. Overloaded function used by
    /// bulk_load().
    inline void set_slot(unsigned short slot, const pair_type& value) {
      BTREE_ASSERT(used_as_set == false);BTREE_ASSERT(slot < node::slotuse);
      slotkey[slot] = value.first;
      slotdata[slot] = value.second;
    }

    /// Set the key pair in slot. Overloaded function used by
    /// bulk_load().
    inline void set_slot(unsigned short slot, const key_type& key) {
      BTREE_ASSERT(used_as_set == true);BTREE_ASSERT(slot < node::slotuse);
      slotkey[slot] = key;
    }
  };

 private:
  // *** Template Magic to Convert a pair or key/data types to a value_type

  /// For sets the second pair_type is an empty struct, so the value_type
  /// should only be the first.
  template<typename value_type, typename pair_type>
  struct btree_pair_to_value {
    /// Convert a fake pair type to just the first component
    inline value_type operator()(pair_type& p) const {
      return p.first;
    }
    /// Convert a fake pair type to just the first component
    inline value_type operator()(const pair_type& p) const {
      return p.first;
    }
  };

  /// For maps value_type is the same as the pair_type
  template<typename value_type>
  struct btree_pair_to_value<value_type, value_type> {
    /// Identity "convert" a real pair type to just the first component
    inline value_type operator()(pair_type& p) const {
      return p;
    }
    /// Identity "convert" a real pair type to just the first component
    inline value_type operator()(const pair_type& p) const {
      return p;
    }
  };

  /// Using template specialization select the correct converter used by the
  /// iterators
  typedef btree_pair_to_value<value_type, pair_type> pair_to_value_type;

 public:
  // *** Iterators and Reverse Iterators

  class iterator;
  class const_iterator;
  class reverse_iterator;
  class const_reverse_iterator;

  /// STL-like iterator object for B+ tree items. The iterator points to a
  /// specific slot number in a leaf.
  class iterator {
   public:
    // *** Types

    /// The key type of the btree. Returned by key().
    typedef typename btree::key_type key_type;

    /// The data type of the btree. Returned by data().
    typedef typename btree::data_type data_type;

    /// The value type of the btree. Returned by operator*().
    typedef typename btree::value_type value_type;

    /// The pair type of the btree.
    typedef typename btree::pair_type pair_type;

    /// Reference to the value_type. STL required.
    typedef value_type& reference;

    /// Pointer to the value_type. STL required.
    typedef value_type* pointer;

    /// STL-magic iterator category
    typedef std::bidirectional_iterator_tag iterator_category;

    /// STL-magic
    typedef ptrdiff_t difference_type;

    /// Our own type
    typedef iterator self;

   private:
    // *** Members

    /// The currently referenced leaf node of the tree
    typename btree::leaf_node* currnode;

    /// Current key/data slot referenced
    unsigned short currslot;

    /// Friendly to the const_iterator, so it may access the two data items
    /// directly.
    friend class const_iterator;

    /// Also friendly to the reverse_iterator, so it may access the two
    /// data items directly.
    friend class reverse_iterator;

    /// Also friendly to the const_reverse_iterator, so it may access the
    /// two data items directly.
    friend class const_reverse_iterator;

    /// Also friendly to the base btree class, because erase_iter() needs
    /// to read the currnode and currslot values directly.
    friend class btree<key_type, data_type, value_type, key_compare, traits,
        allow_duplicates, allocator_type, used_as_set> ;

    /// Evil! A temporary value_type to STL-correctly deliver operator* and
    /// operator->
    mutable value_type temp_value;

    // The macro BTREE_FRIENDS can be used by outside class to access the B+
    // tree internals. This was added for wxBTreeDemo to be able to draw the
    // tree.
    BTREE_FRIENDS

   public:
    // *** Methods

    /// Default-Constructor of a mutable iterator
    inline iterator()
        : currnode(NULL),
          currslot(0) {
    }

    /// Initializing-Constructor of a mutable iterator
    inline iterator(typename btree::leaf_node *l, unsigned short s)
        : currnode(l),
          currslot(s) {
    }

    /// Copy-constructor from a reverse iterator
    inline iterator(const reverse_iterator &it)
        : currnode(it.currnode),
          currslot(it.currslot) {
    }

    /// Dereference the iterator, this is not a value_type& because key and
    /// value are not stored together
    inline reference operator*() const {
      temp_value = pair_to_value_type()(pair_type(key(), data()));
      return temp_value;
    }

    /// Dereference the iterator. Do not use this if possible, use key()
    /// and data() instead. The B+ tree does not stored key and data
    /// together.
    inline pointer operator->() const {
      temp_value = pair_to_value_type()(pair_type(key(), data()));
      return &temp_value;
    }

    /// Key of the current slot
    inline const key_type& key() const {
      return currnode->slotkey[currslot];
    }

    /// Writable reference to the current data object
    inline data_type& data() const {
      return currnode->slotdata[used_as_set ? 0 : currslot];
    }

    /// Prefix++ advance the iterator to the next slot
    inline self& operator++() {
      if (currslot + 1 < currnode->slotuse) {
        ++currslot;
      } else if (currnode->nextleaf != NULL) {
        currnode = currnode->nextleaf;
        currslot = 0;
      } else {
        // this is end()
        currslot = currnode->slotuse;
      }

      return *this;
    }

    /// Postfix++ advance the iterator to the next slot
    inline self operator++(int) {
      self tmp = *this;   // copy ourselves

      if (currslot + 1 < currnode->slotuse) {
        ++currslot;
      } else if (currnode->nextleaf != NULL) {
        currnode = currnode->nextleaf;
        currslot = 0;
      } else {
        // this is end()
        currslot = currnode->slotuse;
      }

      return tmp;
    }

    /// Prefix-- backstep the iterator to the last slot
    inline self& operator--() {
      if (currslot > 0) {
        --currslot;
      } else if (currnode->prevleaf != NULL) {
        currnode = currnode->prevleaf;
        currslot = currnode->slotuse - 1;
      } else {
        // this is begin()
        currslot = 0;
      }

      return *this;
    }

    /// Postfix-- backstep the iterator to the last slot
    inline self operator--(int) {
      self tmp = *this;   // copy ourselves

      if (currslot > 0) {
        --currslot;
      } else if (currnode->prevleaf != NULL) {
        currnode = currnode->prevleaf;
        currslot = currnode->slotuse - 1;
      } else {
        // this is begin()
        currslot = 0;
      }

      return tmp;
    }

    /// Equality of iterators
    inline bool operator==(const self& x) const {
      return (x.currnode == currnode) && (x.currslot == currslot);
    }

    /// Inequality of iterators
    inline bool operator!=(const self& x) const {
      return (x.currnode != currnode) || (x.currslot != currslot);
    }
  };

  /// STL-like read-only iterator object for B+ tree items. The iterator
  /// points to a specific slot number in a leaf.
  class const_iterator {
   public:
    // *** Types

    /// The key type of the btree. Returned by key().
    typedef typename btree::key_type key_type;

    /// The data type of the btree. Returned by data().
    typedef typename btree::data_type data_type;

    /// The value type of the btree. Returned by operator*().
    typedef typename btree::value_type value_type;

    /// The pair type of the btree.
    typedef typename btree::pair_type pair_type;

    /// Reference to the value_type. STL required.
    typedef const value_type& reference;

    /// Pointer to the value_type. STL required.
    typedef const value_type* pointer;

    /// STL-magic iterator category
    typedef std::bidirectional_iterator_tag iterator_category;

    /// STL-magic
    typedef ptrdiff_t difference_type;

    /// Our own type
    typedef const_iterator self;

   private:
    // *** Members

    /// The currently referenced leaf node of the tree
    const typename btree::leaf_node* currnode;

    /// Current key/data slot referenced
    unsigned short currslot;

    /// Friendly to the reverse_const_iterator, so it may access the two
    /// data items directly
    friend class const_reverse_iterator;

    /// Evil! A temporary value_type to STL-correctly deliver operator* and
    /// operator->
    mutable value_type temp_value;

    // The macro BTREE_FRIENDS can be used by outside class to access the B+
    // tree internals. This was added for wxBTreeDemo to be able to draw the
    // tree.
    BTREE_FRIENDS

   public:
    // *** Methods

    /// Default-Constructor of a const iterator
    inline const_iterator()
        : currnode(NULL),
          currslot(0) {
    }

    /// Initializing-Constructor of a const iterator
    inline const_iterator(const typename btree::leaf_node *l, unsigned short s)
        : currnode(l),
          currslot(s) {
    }

    /// Copy-constructor from a mutable iterator
    inline const_iterator(const iterator &it)
        : currnode(it.currnode),
          currslot(it.currslot) {
    }

    /// Copy-constructor from a mutable reverse iterator
    inline const_iterator(const reverse_iterator &it)
        : currnode(it.currnode),
          currslot(it.currslot) {
    }

    /// Copy-constructor from a const reverse iterator
    inline const_iterator(const const_reverse_iterator &it)
        : currnode(it.currnode),
          currslot(it.currslot) {
    }

    /// Dereference the iterator. Do not use this if possible, use key()
    /// and data() instead. The B+ tree does not stored key and data
    /// together.
    inline reference operator*() const {
      temp_value = pair_to_value_type()(pair_type(key(), data()));
      return temp_value;
    }

    /// Dereference the iterator. Do not use this if possible, use key()
    /// and data() instead. The B+ tree does not stored key and data
    /// together.
    inline pointer operator->() const {
      temp_value = pair_to_value_type()(pair_type(key(), data()));
      return &temp_value;
    }

    /// Key of the current slot
    inline const key_type& key() const {
      return currnode->slotkey[currslot];
    }

    /// Read-only reference to the current data object
    inline const data_type& data() const {
      return currnode->slotdata[used_as_set ? 0 : currslot];
    }

    /// Prefix++ advance the iterator to the next slot
    inline self& operator++() {
      if (currslot + 1 < currnode->slotuse) {
        ++currslot;
      } else if (currnode->nextleaf != NULL) {
        currnode = currnode->nextleaf;
        currslot = 0;
      } else {
        // this is end()
        currslot = currnode->slotuse;
      }

      return *this;
    }

    /// Postfix++ advance the iterator to the next slot
    inline self operator++(int) {
      self tmp = *this;   // copy ourselves

      if (currslot + 1 < currnode->slotuse) {
        ++currslot;
      } else if (currnode->nextleaf != NULL) {
        currnode = currnode->nextleaf;
        currslot = 0;
      } else {
        // this is end()
        currslot = currnode->slotuse;
      }

      return tmp;
    }

    /// Prefix-- backstep the iterator to the last slot
    inline self& operator--() {
      if (currslot > 0) {
        --currslot;
      } else if (currnode->prevleaf != NULL) {
        currnode = currnode->prevleaf;
        currslot = currnode->slotuse - 1;
      } else {
        // this is begin()
        currslot = 0;
      }

      return *this;
    }

    /// Postfix-- backstep the iterator to the last slot
    inline self operator--(int) {
      self tmp = *this;   // copy ourselves

      if (currslot > 0) {
        --currslot;
      } else if (currnode->prevleaf != NULL) {
        currnode = currnode->prevleaf;
        currslot = currnode->slotuse - 1;
      } else {
        // this is begin()
        currslot = 0;
      }

      return tmp;
    }

    /// Equality of iterators
    inline bool operator==(const self& x) const {
      return (x.currnode == currnode) && (x.currslot == currslot);
    }

    /// Inequality of iterators
    inline bool operator!=(const self& x) const {
      return (x.currnode != currnode) || (x.currslot != currslot);
    }
  };

  /// STL-like mutable reverse iterator object for B+ tree items. The
  /// iterator points to a specific slot number in a leaf.
  class reverse_iterator {
   public:
    // *** Types

    /// The key type of the btree. Returned by key().
    typedef typename btree::key_type key_type;

    /// The data type of the btree. Returned by data().
    typedef typename btree::data_type data_type;

    /// The value type of the btree. Returned by operator*().
    typedef typename btree::value_type value_type;

    /// The pair type of the btree.
    typedef typename btree::pair_type pair_type;

    /// Reference to the value_type. STL required.
    typedef value_type& reference;

    /// Pointer to the value_type. STL required.
    typedef value_type* pointer;

    /// STL-magic iterator category
    typedef std::bidirectional_iterator_tag iterator_category;

    /// STL-magic
    typedef ptrdiff_t difference_type;

    /// Our own type
    typedef reverse_iterator self;

   private:
    // *** Members

    /// The currently referenced leaf node of the tree
    typename btree::leaf_node* currnode;

    /// One slot past the current key/data slot referenced.
    unsigned short currslot;

    /// Friendly to the const_iterator, so it may access the two data items
    /// directly
    friend class iterator;

    /// Also friendly to the const_iterator, so it may access the two data
    /// items directly
    friend class const_iterator;

    /// Also friendly to the const_iterator, so it may access the two data
    /// items directly
    friend class const_reverse_iterator;

    /// Evil! A temporary value_type to STL-correctly deliver operator* and
    /// operator->
    mutable value_type temp_value;

    // The macro BTREE_FRIENDS can be used by outside class to access the B+
    // tree internals. This was added for wxBTreeDemo to be able to draw the
    // tree.
    BTREE_FRIENDS

   public:
    // *** Methods

    /// Default-Constructor of a reverse iterator
    inline reverse_iterator()
        : currnode(NULL),
          currslot(0) {
    }

    /// Initializing-Constructor of a mutable reverse iterator
    inline reverse_iterator(typename btree::leaf_node *l, unsigned short s)
        : currnode(l),
          currslot(s) {
    }

    /// Copy-constructor from a mutable iterator
    inline reverse_iterator(const iterator &it)
        : currnode(it.currnode),
          currslot(it.currslot) {
    }

    /// Dereference the iterator, this is not a value_type& because key and
    /// value are not stored together
    inline reference operator*() const {
      BTREE_ASSERT(currslot > 0);
      temp_value = pair_to_value_type()(pair_type(key(), data()));
      return temp_value;
    }

    /// Dereference the iterator. Do not use this if possible, use key()
    /// and data() instead. The B+ tree does not stored key and data
    /// together.
    inline pointer operator->() const {
      BTREE_ASSERT(currslot > 0);
      temp_value = pair_to_value_type()(pair_type(key(), data()));
      return &temp_value;
    }

    /// Key of the current slot
    inline const key_type& key() const {
      BTREE_ASSERT(currslot > 0);
      return currnode->slotkey[currslot - 1];
    }

    /// Writable reference to the current data object
    inline data_type& data() const {
      BTREE_ASSERT(currslot > 0);
      return currnode->slotdata[used_as_set ? 0 : currslot - 1];
    }

    /// Prefix++ advance the iterator to the next slot
    inline self& operator++() {
      if (currslot > 1) {
        --currslot;
      } else if (currnode->prevleaf != NULL) {
        currnode = currnode->prevleaf;
        currslot = currnode->slotuse;
      } else {
        // this is begin() == rend()
        currslot = 0;
      }

      return *this;
    }

    /// Postfix++ advance the iterator to the next slot
    inline self operator++(int) {
      self tmp = *this;   // copy ourselves

      if (currslot > 1) {
        --currslot;
      } else if (currnode->prevleaf != NULL) {
        currnode = currnode->prevleaf;
        currslot = currnode->slotuse;
      } else {
        // this is begin() == rend()
        currslot = 0;
      }

      return tmp;
    }

    /// Prefix-- backstep the iterator to the last slot
    inline self& operator--() {
      if (currslot < currnode->slotuse) {
        ++currslot;
      } else if (currnode->nextleaf != NULL) {
        currnode = currnode->nextleaf;
        currslot = 1;
      } else {
        // this is end() == rbegin()
        currslot = currnode->slotuse;
      }

      return *this;
    }

    /// Postfix-- backstep the iterator to the last slot
    inline self operator--(int) {
      self tmp = *this;   // copy ourselves

      if (currslot < currnode->slotuse) {
        ++currslot;
      } else if (currnode->nextleaf != NULL) {
        currnode = currnode->nextleaf;
        currslot = 1;
      } else {
        // this is end() == rbegin()
        currslot = currnode->slotuse;
      }

      return tmp;
    }

    /// Equality of iterators
    inline bool operator==(const self& x) const {
      return (x.currnode == currnode) && (x.currslot == currslot);
    }

    /// Inequality of iterators
    inline bool operator!=(const self& x) const {
      return (x.currnode != currnode) || (x.currslot != currslot);
    }
  };

  /// STL-like read-only reverse iterator object for B+ tree items. The
  /// iterator points to a specific slot number in a leaf.
  class const_reverse_iterator {
   public:
    // *** Types

    /// The key type of the btree. Returned by key().
    typedef typename btree::key_type key_type;

    /// The data type of the btree. Returned by data().
    typedef typename btree::data_type data_type;

    /// The value type of the btree. Returned by operator*().
    typedef typename btree::value_type value_type;

    /// The pair type of the btree.
    typedef typename btree::pair_type pair_type;

    /// Reference to the value_type. STL required.
    typedef const value_type& reference;

    /// Pointer to the value_type. STL required.
    typedef const value_type* pointer;

    /// STL-magic iterator category
    typedef std::bidirectional_iterator_tag iterator_category;

    /// STL-magic
    typedef ptrdiff_t difference_type;

    /// Our own type
    typedef const_reverse_iterator self;

   private:
    // *** Members

    /// The currently referenced leaf node of the tree
    const typename btree::leaf_node* currnode;

    /// One slot past the current key/data slot referenced.
    unsigned short currslot;

    /// Friendly to the const_iterator, so it may access the two data items
    /// directly.
    friend class reverse_iterator;

    /// Evil! A temporary value_type to STL-correctly deliver operator* and
    /// operator->
    mutable value_type temp_value;

    // The macro BTREE_FRIENDS can be used by outside class to access the B+
    // tree internals. This was added for wxBTreeDemo to be able to draw the
    // tree.
    BTREE_FRIENDS

   public:
    // *** Methods

    /// Default-Constructor of a const reverse iterator
    inline const_reverse_iterator()
        : currnode(NULL),
          currslot(0) {
    }

    /// Initializing-Constructor of a const reverse iterator
    inline const_reverse_iterator(const typename btree::leaf_node *l,
                                  unsigned short s)
        : currnode(l),
          currslot(s) {
    }

    /// Copy-constructor from a mutable iterator
    inline const_reverse_iterator(const iterator &it)
        : currnode(it.currnode),
          currslot(it.currslot) {
    }

    /// Copy-constructor from a const iterator
    inline const_reverse_iterator(const const_iterator &it)
        : currnode(it.currnode),
          currslot(it.currslot) {
    }

    /// Copy-constructor from a mutable reverse iterator
    inline const_reverse_iterator(const reverse_iterator &it)
        : currnode(it.currnode),
          currslot(it.currslot) {
    }

    /// Dereference the iterator. Do not use this if possible, use key()
    /// and data() instead. The B+ tree does not stored key and data
    /// together.
    inline reference operator*() const {
      BTREE_ASSERT(currslot > 0);
      temp_value = pair_to_value_type()(pair_type(key(), data()));
      return temp_value;
    }

    /// Dereference the iterator. Do not use this if possible, use key()
    /// and data() instead. The B+ tree does not stored key and data
    /// together.
    inline pointer operator->() const {
      BTREE_ASSERT(currslot > 0);
      temp_value = pair_to_value_type()(pair_type(key(), data()));
      return &temp_value;
    }

    /// Key of the current slot
    inline const key_type& key() const {
      BTREE_ASSERT(currslot > 0);
      return currnode->slotkey[currslot - 1];
    }

    /// Read-only reference to the current data object
    inline const data_type& data() const {
      BTREE_ASSERT(currslot > 0);
      return currnode->slotdata[used_as_set ? 0 : currslot - 1];
    }

    /// Prefix++ advance the iterator to the previous slot
    inline self& operator++() {
      if (currslot > 1) {
        --currslot;
      } else if (currnode->prevleaf != NULL) {
        currnode = currnode->prevleaf;
        currslot = currnode->slotuse;
      } else {
        // this is begin() == rend()
        currslot = 0;
      }

      return *this;
    }

    /// Postfix++ advance the iterator to the previous slot
    inline self operator++(int) {
      self tmp = *this;   // copy ourselves

      if (currslot > 1) {
        --currslot;
      } else if (currnode->prevleaf != NULL) {
        currnode = currnode->prevleaf;
        currslot = currnode->slotuse;
      } else {
        // this is begin() == rend()
        currslot = 0;
      }

      return tmp;
    }

    /// Prefix-- backstep the iterator to the next slot
    inline self& operator--() {
      if (currslot < currnode->slotuse) {
        ++currslot;
      } else if (currnode->nextleaf != NULL) {
        currnode = currnode->nextleaf;
        currslot = 1;
      } else {
        // this is end() == rbegin()
        currslot = currnode->slotuse;
      }

      return *this;
    }

    /// Postfix-- backstep the iterator to the next slot
    inline self operator--(int) {
      self tmp = *this;   // copy ourselves

      if (currslot < currnode->slotuse) {
        ++currslot;
      } else if (currnode->nextleaf != NULL) {
        currnode = currnode->nextleaf;
        currslot = 1;
      } else {
        // this is end() == rbegin()
        currslot = currnode->slotuse;
      }

      return tmp;
    }

    /// Equality of iterators
    inline bool operator==(const self& x) const {
      return (x.currnode == currnode) && (x.currslot == currslot);
    }

    /// Inequality of iterators
    inline bool operator!=(const self& x) const {
      return (x.currnode != currnode) || (x.currslot != currslot);
    }
  };

 public:
  // *** Small Statistics Structure

  /** A small struct containing basic statistics about the B+ tree. It can be
   * fetched using get_stats(). */
  struct tree_stats {
    /// Number of items in the B+ tree
    size_type itemcount;

    /// Number of leaves in the B+ tree
    size_type leaves;

    /// Number of inner nodes in the B+ tree
    size_type innernodes;

    /// Base B+ tree parameter: The number of key/data slots in each leaf
    static const unsigned short leafslots = btree_self::leafslotmax;

    /// Base B+ tree parameter: The number of key slots in each inner node.
    static const unsigned short innerslots = btree_self::innerslotmax;

    /// Zero initialized
    inline tree_stats()
        : itemcount(0),
          leaves(0),
          innernodes(0) {
    }

    /// Return the total number of nodes
    inline size_type nodes() const {
      return innernodes + leaves;
    }

    /// Return the average fill of leaves
    inline double avgfill_leaves() const {
      return static_cast<double>(itemcount) / (leaves * leafslots);
    }
  };

 private:
  // *** Tree Object Data Members

  /// Pointer to the B+ tree's root node, either leaf or inner node
  node** m_root;

  /// Pointer to first leaf in the double linked leaf chain
  leaf_node *m_headleaf;

  /// Pointer to last leaf in the double linked leaf chain
  leaf_node *m_tailleaf;

  /// Other small statistics about the B+ tree
  tree_stats *m_stats;

  /// Key comparison object. More comparison functions are generated from
  /// this < relation.
  key_compare m_key_less;

  /// Memory allocator.
  allocator_type m_allocator;

  // Persistence mode
  bool persist = true;

 public:
  // *** Constructors and Destructor

  /// Default constructor initializing an empty B+ tree with the standard key
  /// comparison function
  explicit inline btree(void** _root, const allocator_type &alloc =
                            allocator_type())
      : m_root((node**) _root),
        m_headleaf(NULL),
        m_tailleaf(NULL),
        m_allocator(alloc) {
    if (m_stats == NULL) {
      m_stats = new tree_stats;

      if (persist)
        pmemalloc_activate(m_stats);
      else
        pmemalloc_count(m_stats);
    }
  }

  /// Constructor initializing an empty B+ tree with a special key
  /// comparison object
  explicit inline btree(const key_compare &kcf, const allocator_type &alloc =
                            allocator_type())
      : m_root(NULL),
        m_headleaf(NULL),
        m_tailleaf(NULL),
        m_key_less(kcf),
        m_allocator(alloc),
        m_stats(NULL) {
  }

  /// Constructor initializing a B+ tree with the range [first,last). The
  /// range need not be sorted. To create a B+ tree from a sorted range, use
  /// bulk_load().
  template<class InputIterator>
  inline btree(InputIterator first, InputIterator last,
               const allocator_type &alloc = allocator_type())
      : m_root(NULL),
        m_headleaf(NULL),
        m_tailleaf(NULL),
        m_allocator(alloc),
        m_stats(NULL) {
    insert(first, last);
  }

  /// Constructor initializing a B+ tree with the range [first,last) and a
  /// special key comparison object.  The range need not be sorted. To create
  /// a B+ tree from a sorted range, use bulk_load().
  template<class InputIterator>
  inline btree(InputIterator first, InputIterator last, const key_compare &kcf,
               const allocator_type &alloc = allocator_type())
      : m_root(NULL),
        m_headleaf(NULL),
        m_tailleaf(NULL),
        m_key_less(kcf),
        m_allocator(alloc),
        m_stats(NULL) {
    insert(first, last);
  }

  /// Frees up all used B+ tree memory pages
  inline ~btree() {
    clear();
  }

  /// Fast swapping of two identical B+ tree objects.
  void swap(btree_self& from) {
    std::swap(m_root, from.m_root);
    std::swap(m_headleaf, from.m_headleaf);
    std::swap(m_tailleaf, from.m_tailleaf);
    std::swap(m_stats, from.m_stats);
    std::swap(m_key_less, from.m_key_less);
    std::swap(m_allocator, from.m_allocator);
  }

 public:
  // *** Key and Value Comparison Function Objects

  /// Function class to compare value_type objects. Required by the STL
  class value_compare {
   protected:
    /// Key comparison function from the template parameter
    key_compare key_comp;

    /// Constructor called from btree::value_comp()
    inline value_compare(key_compare kc)
        : key_comp(kc) {
    }

    /// Friendly to the btree class so it may call the constructor
    friend class btree<key_type, data_type, value_type, key_compare, traits,
        allow_duplicates, allocator_type, used_as_set> ;

   public:
    /// Function call "less"-operator resulting in true if x < y.
    inline bool operator()(const value_type& x, const value_type& y) const {
      return key_comp(x.first, y.first);
    }
  };

  /// Constant access to the key comparison object sorting the B+ tree
  inline key_compare key_comp() const {
    return m_key_less;
  }

  /// Constant access to a constructed value_type comparison object. Required
  /// by the STL
  inline value_compare value_comp() const {
    return value_compare(m_key_less);
  }

 private:
  // *** Convenient Key Comparison Functions Generated From key_less

  /// True if a < b ? "constructed" from m_key_less()
  inline bool key_less(const key_type &a, const key_type b) const {
    return m_key_less(a, b);
  }

  /// True if a <= b ? constructed from key_less()
  inline bool key_lessequal(const key_type &a, const key_type b) const {
    return !m_key_less(b, a);
  }

  /// True if a > b ? constructed from key_less()
  inline bool key_greater(const key_type &a, const key_type &b) const {
    return m_key_less(b, a);
  }

  /// True if a >= b ? constructed from key_less()
  inline bool key_greaterequal(const key_type &a, const key_type b) const {
    return !m_key_less(a, b);
  }

  /// True if a == b ? constructed from key_less(). This requires the <
  /// relation to be a total order, otherwise the B+ tree cannot be sorted.
  inline bool key_equal(const key_type &a, const key_type &b) const {
    return !m_key_less(a, b) && !m_key_less(b, a);
  }

 public:
  // *** Allocators

  /// Return the base node allocator provided during construction.
  allocator_type get_allocator() const {
    return m_allocator;
  }

 private:
  // *** Node Object Allocation and Deallocation Functions

  /// Return an allocator for leaf_node objects
  typename leaf_node::alloc_type leaf_node_allocator() {
    return typename leaf_node::alloc_type(m_allocator);
  }

  /// Return an allocator for inner_node objects
  typename inner_node::alloc_type inner_node_allocator() {
    return typename inner_node::alloc_type(m_allocator);
  }

  /// Allocate and initialize a leaf node
  inline leaf_node* allocate_leaf() {
    leaf_node *n = new (leaf_node_allocator().allocate(1)) leaf_node();
    if (persist)
      pmemalloc_activate(n);
    else
      pmemalloc_count(n);

    n->initialize();
    m_stats->leaves++;
    return n;
  }

  /// Allocate and initialize an inner node
  inline inner_node* allocate_inner(unsigned short level) {
    inner_node *n = new (inner_node_allocator().allocate(1)) inner_node();
    if (persist)
      pmemalloc_activate(n);
    else
      pmemalloc_count(n);

    n->initialize(level);
    m_stats->innernodes++;
    return n;
  }

  /// Correctly free either inner or leaf node, destructs all contained key
  /// and value objects
  inline void free_node(node *n) {
    if (n->isleafnode()) {
      leaf_node *ln = static_cast<leaf_node*>(n);
      typename leaf_node::alloc_type a(leaf_node_allocator());
      a.destroy(ln);
      a.deallocate(ln, 1);
      m_stats->leaves--;
    } else {
      inner_node *in = static_cast<inner_node*>(n);
      typename inner_node::alloc_type a(inner_node_allocator());
      a.destroy(in);
      a.deallocate(in, 1);
      m_stats->innernodes--;
    }
  }

  /// Convenient template function for conditional copying of slotdata. This
  /// should be used instead of std::copy for all slotdata manipulations.
  template<class InputIterator, class OutputIterator>
  static OutputIterator data_copy(InputIterator first, InputIterator last,
                                  OutputIterator result) {
    if (used_as_set)
      return result;  // no operation
    else
      return std::copy(first, last, result);
  }

  /// Convenient template function for conditional copying of slotdata. This
  /// should be used instead of std::copy for all slotdata manipulations.
  template<class InputIterator, class OutputIterator>
  static OutputIterator data_copy_backward(InputIterator first,
                                           InputIterator last,
                                           OutputIterator result) {
    if (used_as_set)
      return result;  // no operation
    else
      return std::copy_backward(first, last, result);
  }

 public:
  // *** Fast Destruction of the B+ Tree

  /// Frees all key/data pairs and all nodes of the tree
  void clear() {
    if ((*m_root)) {
      clear_recursive((*m_root));
      free_node((*m_root));

      (*m_root) = NULL;
      m_headleaf = m_tailleaf = NULL;
    }

    m_stats->itemcount = 0;
    //assert(m_stats->itemcount == 0);
  }

 private:
  /// Recursively free up nodes
  void clear_recursive(node *n) {
    if (n->isleafnode()) {
      leaf_node *leafnode = static_cast<leaf_node*>(n);

      for (unsigned int slot = 0; slot < leafnode->slotuse; ++slot) {
        // data objects are deleted by leaf_node's destructor
      }
    } else {
      inner_node *innernode = static_cast<inner_node*>(n);

      for (unsigned short slot = 0; slot < innernode->slotuse + 1; ++slot) {
        clear_recursive(innernode->childid[slot]);
        free_node(innernode->childid[slot]);
      }
    }
  }

 public:
  // *** STL Iterator Construction Functions

  /// Constructs a read/data-write iterator that points to the first slot in
  /// the first leaf of the B+ tree.
  inline iterator begin() {
    return iterator(m_headleaf, 0);
  }

  /// Constructs a read/data-write iterator that points to the first invalid
  /// slot in the last leaf of the B+ tree.
  inline iterator end() {
    return iterator(m_tailleaf, m_tailleaf ? m_tailleaf->slotuse : 0);
  }

  /// Constructs a read-only constant iterator that points to the first slot
  /// in the first leaf of the B+ tree.
  inline const_iterator begin() const {
    return const_iterator(m_headleaf, 0);
  }

  /// Constructs a read-only constant iterator that points to the first
  /// invalid slot in the last leaf of the B+ tree.
  inline const_iterator end() const {
    return const_iterator(m_tailleaf, m_tailleaf ? m_tailleaf->slotuse : 0);
  }

  /// Constructs a read/data-write reverse iterator that points to the first
  /// invalid slot in the last leaf of the B+ tree. Uses STL magic.
  inline reverse_iterator rbegin() {
    return reverse_iterator(end());
  }

  /// Constructs a read/data-write reverse iterator that points to the first
  /// slot in the first leaf of the B+ tree. Uses STL magic.
  inline reverse_iterator rend() {
    return reverse_iterator(begin());
  }

  /// Constructs a read-only reverse iterator that points to the first
  /// invalid slot in the last leaf of the B+ tree. Uses STL magic.
  inline const_reverse_iterator rbegin() const {
    return const_reverse_iterator(end());
  }

  /// Constructs a read-only reverse iterator that points to the first slot
  /// in the first leaf of the B+ tree. Uses STL magic.
  inline const_reverse_iterator rend() const {
    return const_reverse_iterator(begin());
  }

 private:
  // *** B+ Tree Node Binary Search Functions

  /// Searches for the first key in the node n greater or equal to key. Uses
  /// binary search with an optional linear self-verification. This is a
  /// template function, because the slotkey array is located at different
  /// places in leaf_node and inner_node.
  template<typename node_type>
  inline int find_lower(const node_type *n, const key_type& key) const {
    if (0 && sizeof(n->slotkey) > traits::binsearch_threshold) {
      if (n->slotuse == 0)
        return 0;

      int lo = 0, hi = n->slotuse;

      while (lo < hi) {
        int mid = (lo + hi) >> 1;

        if (key_lessequal(key, n->slotkey[mid])) {
          hi = mid;  // key <= mid
        } else {
          lo = mid + 1;  // key > mid
        }
      }

      BTREE_PRINT(
          "btree::find_lower: on " << n << " key " << key << " -> " << lo << " / " << hi);

      // verify result using simple linear search
      if (selfverify) {
        int i = 0;
        while (i < n->slotuse && key_less(n->slotkey[i], key))
          ++i;

        BTREE_PRINT("btree::find_lower: testfind: " << i);BTREE_ASSERT(i == lo);
      }

      return lo;
    } else  // for nodes <= binsearch_threshold do linear search.
    {
      int lo = 0;
      while (lo < n->slotuse && key_less(n->slotkey[lo], key))
        ++lo;
      return lo;
    }
  }

  /// Searches for the first key in the node n greater than key. Uses binary
  /// search with an optional linear self-verification. This is a template
  /// function, because the slotkey array is located at different places in
  /// leaf_node and inner_node.
  template<typename node_type>
  inline int find_upper(const node_type *n, const key_type& key) const {
    if (0 && sizeof(n->slotkey) > traits::binsearch_threshold) {
      if (n->slotuse == 0)
        return 0;

      int lo = 0, hi = n->slotuse;

      while (lo < hi) {
        int mid = (lo + hi) >> 1;

        if (key_less(key, n->slotkey[mid])) {
          hi = mid;  // key < mid
        } else {
          lo = mid + 1;  // key >= mid
        }
      }

      BTREE_PRINT(
          "btree::find_upper: on " << n << " key " << key << " -> " << lo << " / " << hi);

      // verify result using simple linear search
      if (selfverify) {
        int i = 0;
        while (i < n->slotuse && key_lessequal(n->slotkey[i], key))
          ++i;

        BTREE_PRINT("btree::find_upper testfind: " << i);BTREE_ASSERT(i == hi);
      }

      return lo;
    } else  // for nodes <= binsearch_threshold do linear search.
    {
      int lo = 0;
      while (lo < n->slotuse && key_lessequal(n->slotkey[lo], key))
        ++lo;
      return lo;
    }
  }

 public:
  // *** Access Functions to the Item Count

  /// Return the number of key/data pairs in the B+ tree
  inline size_type size() const {
    return m_stats->itemcount;
  }

  /// Returns true if there is at least one key/data pair in the B+ tree
  inline bool empty() const {
    return (size() == size_type(0));
  }

  /// Returns the largest possible size of the B+ Tree. This is just a
  /// function required by the STL standard, the B+ Tree can hold more items.
  inline size_type max_size() const {
    return size_type(-1);
  }

  /// Return a const reference to the current statistics.
  inline const struct tree_stats& get_stats() const {
    return m_stats;
  }

 public:
  // *** Standard Access Functions Querying the Tree by Descending to a Leaf

  /// Non-STL function checking whether a key is in the B+ tree. The same as
  /// (find(k) != end()) or (count() != 0).
  bool exists(const key_type &key) const {
    const node *n = (*m_root);
    if (!n)
      return false;

    while (!n->isleafnode()) {
      const inner_node *inner = static_cast<const inner_node*>(n);
      int slot = find_lower(inner, key);

      n = inner->childid[slot];
    }

    const leaf_node *leaf = static_cast<const leaf_node*>(n);

    int slot = find_lower(leaf, key);
    return (slot < leaf->slotuse && key_equal(key, leaf->slotkey[slot]));
  }

  /// Tries to locate a key in the B+ tree and returns an iterator to the
  /// key/data slot if found. If unsuccessful it returns end().
  iterator find(const key_type &key) {
    node *n = (*m_root);
    if (!n)
      return end();

    while (!n->isleafnode()) {
      const inner_node *inner = static_cast<const inner_node*>(n);
      int slot = find_lower(inner, key);

      n = inner->childid[slot];
    }

    leaf_node *leaf = static_cast<leaf_node*>(n);

    int slot = find_lower(leaf, key);
    return
        (slot < leaf->slotuse && key_equal(key, leaf->slotkey[slot])) ?
            iterator(leaf, slot) : end();
  }

  /// Tries to locate a key in the B+ tree and returns an constant iterator
  /// to the key/data slot if found. If unsuccessful it returns end().
  const_iterator find(const key_type &key) const {
    const node *n = (*m_root);
    if (!n)
      return end();

    while (!n->isleafnode()) {
      const inner_node *inner = static_cast<const inner_node*>(n);
      int slot = find_lower(inner, key);

      n = inner->childid[slot];
    }

    const leaf_node *leaf = static_cast<const leaf_node*>(n);

    int slot = find_lower(leaf, key);
    return
        (slot < leaf->slotuse && key_equal(key, leaf->slotkey[slot])) ?
            const_iterator(leaf, slot) : end();
  }

  /// Tries to return value if key is found.
  bool at(const key_type &key, data_type* val) const {
    const_iterator itr = find(key);
    bool found = false;

    if (itr != end()) {
      (*val) = itr.data();
      found = true;
    }

    return found;
  }

  /// Tries to set value if key is found.
  int update(const key_type &key, const data_type &val) {
    iterator itr = find(key);
    int ret = -1;

    if (itr != end()) {
      itr.data() = val;
      ret = 0;
    }

    return ret;
  }

  // Disable persistence
  void disable_persistence() {
    persist = false;
  }

  /// Tries to locate a key in the B+ tree and returns the number of
  /// identical key entries found.
  size_type count(const key_type &key) const {
    const node *n = (*m_root);
    if (!n)
      return 0;

    while (!n->isleafnode()) {
      const inner_node *inner = static_cast<const inner_node*>(n);
      int slot = find_lower(inner, key);

      n = inner->childid[slot];
    }

    const leaf_node *leaf = static_cast<const leaf_node*>(n);

    int slot = find_lower(leaf, key);
    size_type num = 0;

    while (leaf && slot < leaf->slotuse && key_equal(key, leaf->slotkey[slot])) {
      ++num;
      if (++slot >= leaf->slotuse) {
        leaf = leaf->nextleaf;
        slot = 0;
      }
    }

    return num;
  }

  /// Searches the B+ tree and returns an iterator to the first pair
  /// equal to or greater than key, or end() if all keys are smaller.
  iterator lower_bound(const key_type& key) {
    node *n = (*m_root);
    if (!n)
      return end();

    while (!n->isleafnode()) {
      const inner_node *inner = static_cast<const inner_node*>(n);
      int slot = find_lower(inner, key);

      n = inner->childid[slot];
    }

    leaf_node *leaf = static_cast<leaf_node*>(n);

    int slot = find_lower(leaf, key);
    return iterator(leaf, slot);
  }

  /// Searches the B+ tree and returns a constant iterator to the
  /// first pair equal to or greater than key, or end() if all keys
  /// are smaller.
  const_iterator lower_bound(const key_type& key) const {
    const node *n = (*m_root);
    if (!n)
      return end();

    while (!n->isleafnode()) {
      const inner_node *inner = static_cast<const inner_node*>(n);
      int slot = find_lower(inner, key);

      n = inner->childid[slot];
    }

    const leaf_node *leaf = static_cast<const leaf_node*>(n);

    int slot = find_lower(leaf, key);
    return const_iterator(leaf, slot);
  }

  /// Searches the B+ tree and returns an iterator to the first pair
  /// greater than key, or end() if all keys are smaller or equal.
  iterator upper_bound(const key_type& key) {
    node *n = (*m_root);
    if (!n)
      return end();

    while (!n->isleafnode()) {
      const inner_node *inner = static_cast<const inner_node*>(n);
      int slot = find_upper(inner, key);

      n = inner->childid[slot];
    }

    leaf_node *leaf = static_cast<leaf_node*>(n);

    int slot = find_upper(leaf, key);
    return iterator(leaf, slot);
  }

  /// Searches the B+ tree and returns a constant iterator to the
  /// first pair greater than key, or end() if all keys are smaller
  /// or equal.
  const_iterator upper_bound(const key_type& key) const {
    const node *n = (*m_root);
    if (!n)
      return end();

    while (!n->isleafnode()) {
      const inner_node *inner = static_cast<const inner_node*>(n);
      int slot = find_upper(inner, key);

      n = inner->childid[slot];
    }

    const leaf_node *leaf = static_cast<const leaf_node*>(n);

    int slot = find_upper(leaf, key);
    return const_iterator(leaf, slot);
  }

  /// Searches the B+ tree and returns both lower_bound() and upper_bound().
  inline std::pair<iterator, iterator> equal_range(const key_type& key) {
    return std::pair<iterator, iterator>(lower_bound(key), upper_bound(key));
  }

  /// Searches the B+ tree and returns both lower_bound() and upper_bound().
  inline std::pair<const_iterator, const_iterator> equal_range(
      const key_type& key) const {
    return std::pair<const_iterator, const_iterator>(lower_bound(key),
                                                     upper_bound(key));
  }

 public:
  // *** B+ Tree Object Comparison Functions

  /// Equality relation of B+ trees of the same type. B+ trees of the same
  /// size and equal elements (both key and data) are considered
  /// equal. Beware of the random ordering of duplicate keys.
  inline bool operator==(const btree_self &other) const {
    return (size() == other.size()) && std::equal(begin(), end(), other.begin());
  }

  /// Inequality relation. Based on operator==.
  inline bool operator!=(const btree_self &other) const {
    return !(*this == other);
  }

  /// Total ordering relation of B+ trees of the same type. It uses
  /// std::lexicographical_compare() for the actual comparison of elements.
  inline bool operator<(const btree_self &other) const {
    return std::lexicographical_compare(begin(), end(), other.begin(),
                                        other.end());
  }

  /// Greater relation. Based on operator<.
  inline bool operator>(const btree_self &other) const {
    return other < *this;
  }

  /// Less-equal relation. Based on operator<.
  inline bool operator<=(const btree_self &other) const {
    return !(other < *this);
  }

  /// Greater-equal relation. Based on operator<.
  inline bool operator>=(const btree_self &other) const {
    return !(*this < other);
  }

 public:
  /// *** Fast Copy: Assign Operator and Copy Constructors

  /// Assignment operator. All the key/data pairs are copied
  inline btree_self& operator=(const btree_self &other) {
    if (this != &other) {
      clear();

      m_key_less = other.key_comp();
      m_allocator = other.get_allocator();

      if (other.size() != 0) {
        m_stats->leaves = m_stats->innernodes = 0;
        if (other.m_root) {
          m_root = copy_recursive(other.m_root);
        }
        m_stats = other.m_stats;
      }

      if (selfverify)
        verify();
    }
    return *this;
  }

/// Copy constructor. The newly initialized B+ tree object will contain a
/// copy of all key/data pairs.
  inline btree(const btree_self &other)
      : m_root(NULL),
        m_headleaf(NULL),
        m_tailleaf(NULL),
        m_stats(other.m_stats),
        m_key_less(other.key_comp()),
        m_allocator(other.get_allocator()) {
    if (size() > 0) {
      m_stats->leaves = m_stats->innernodes = 0;
      if (other.m_root) {
        m_root = copy_recursive(other.m_root);
      }
      if (selfverify)
        verify();
    }
  }

 private:
/// Recursively copy nodes from another B+ tree object
  struct node* copy_recursive(const node *n) {
    if (n->isleafnode()) {
      const leaf_node *leaf = static_cast<const leaf_node*>(n);
      leaf_node *newleaf = allocate_leaf();

      newleaf->slotuse = leaf->slotuse;
      std::copy(leaf->slotkey, leaf->slotkey + leaf->slotuse, newleaf->slotkey);
      data_copy(leaf->slotdata, leaf->slotdata + leaf->slotuse,
                newleaf->slotdata);

      if (m_headleaf == NULL) {
        m_headleaf = m_tailleaf = newleaf;
        newleaf->prevleaf = newleaf->nextleaf = NULL;
      } else {
        newleaf->prevleaf = m_tailleaf;
        m_tailleaf->nextleaf = newleaf;
        m_tailleaf = newleaf;
      }

      return newleaf;
    } else {
      const inner_node *inner = static_cast<const inner_node*>(n);
      inner_node *newinner = allocate_inner(inner->level);

      newinner->slotuse = inner->slotuse;
      std::copy(inner->slotkey, inner->slotkey + inner->slotuse,
                newinner->slotkey);

      for (unsigned short slot = 0; slot <= inner->slotuse; ++slot) {
        newinner->childid[slot] = copy_recursive(inner->childid[slot]);
      }

      return newinner;
    }
  }

 public:
// *** Public Insertion Functions

/// Attempt to insert a key/data pair into the B+ tree. If the tree does not
/// allow duplicate keys, then the insert may fail if it is already
/// present.
  inline std::pair<iterator, bool> insert(const pair_type& x) {
    return insert_start(x.first, x.second);
  }

/// Attempt to insert a key/data pair into the B+ tree. Beware that if
/// key_type == data_type, then the template iterator insert() is called
/// instead. If the tree does not allow duplicate keys, then the insert may
/// fail if it is already present.
  inline std::pair<iterator, bool> insert(const key_type& key,
                                          const data_type& data) {
    return insert_start(key, data);
  }

/// Attempt to insert a key/data pair into the B+ tree. This function is the
/// same as the other insert, however if key_type == data_type then the
/// non-template function cannot be called. If the tree does not allow
/// duplicate keys, then the insert may fail if it is already present.
  inline std::pair<iterator, bool> insert2(const key_type& key,
                                           const data_type& data) {
    return insert_start(key, data);
  }

/// Attempt to insert a key/data pair into the B+ tree. The iterator hint
/// is currently ignored by the B+ tree insertion routine.
  inline iterator insert(iterator /* hint */, const pair_type &x) {
    return insert_start(x.first, x.second).first;
  }

/// Attempt to insert a key/data pair into the B+ tree. The iterator hint is
/// currently ignored by the B+ tree insertion routine.
  inline iterator insert2(iterator /* hint */, const key_type& key,
                          const data_type& data) {
    return insert_start(key, data).first;
  }

/// Attempt to insert the range [first,last) of value_type pairs into the
/// B+ tree. Each key/data pair is inserted individually; to bulk load the
/// tree, use a constructor with range.
  template<typename InputIterator>
  inline void insert(InputIterator first, InputIterator last) {
    InputIterator iter = first;
    while (iter != last) {
      insert(*iter);
      ++iter;
    }
  }

 private:
// *** Private Insertion Functions

/// Start the insertion descent at the current root and handle root
/// splits. Returns true if the item was inserted
  std::pair<iterator, bool> insert_start(const key_type& key,
                                         const data_type& value) {
    node *newchild = NULL;
    key_type newkey = key_type();

    if ((*m_root) == NULL) {
      (*m_root) = m_headleaf = m_tailleaf = allocate_leaf();
    }

    std::pair<iterator, bool> r = insert_descend((*m_root), key, value, &newkey,
                                                 &newchild);

    if (newchild) {
      inner_node *newroot = allocate_inner((*m_root)->level + 1);
      newroot->slotkey[0] = newkey;

      newroot->childid[0] = (*m_root);
      newroot->childid[1] = newchild;

      newroot->slotuse = 1;

      (*m_root) = newroot;
    }

    // increment itemcount if the item was inserted
    if (r.second)
      ++m_stats->itemcount;

#ifdef BTREE_DEBUG
    if (debug) print(std::cout);
#endif

    if (selfverify) {
      verify();
      BTREE_ASSERT(exists(key));
    }

    return r;
  }

  /**
   * @brief Insert an item into the B+ tree.
   *
   * Descend down the nodes to a leaf, insert the key/data pair in a free
   * slot. If the node overflows, then it must be split and the new split
   * node inserted into the parent. Unroll / this splitting up to the root.
   */
  std::pair<iterator, bool> insert_descend(node* n, const key_type& key,
                                           const data_type& value,
                                           key_type* splitkey,
                                           node** splitnode) {
    if (!n->isleafnode()) {
      inner_node *inner = static_cast<inner_node*>(n);

      key_type newkey = key_type();
      node *newchild = NULL;

      int slot = find_lower(inner, key);

      BTREE_PRINT("btree::insert_descend into " << inner->childid[slot]);

      std::pair<iterator, bool> r = insert_descend(inner->childid[slot], key,
                                                   value, &newkey, &newchild);

      if (newchild) {
        BTREE_PRINT(
            "btree::insert_descend newchild with key " << newkey << " node " << newchild << " at slot " << slot);

        if (inner->isfull()) {
          split_inner_node(inner, splitkey, splitnode, slot);

          BTREE_PRINT(
              "btree::insert_descend done split_inner: putslot: " << slot << " putkey: " << newkey << " upkey: " << *splitkey);

#ifdef BTREE_DEBUG
          if (debug)
          {
            print_node(std::cout, inner);
            print_node(std::cout, *splitnode);
          }
#endif

          // check if insert slot is in the split sibling node
          BTREE_PRINT(
              "btree::insert_descend switch: " << slot << " > " << inner->slotuse+1);

          if (slot == inner->slotuse + 1
              && inner->slotuse < (*splitnode)->slotuse) {
            // special case when the insert slot matches the split
            // place between the two nodes, then the insert key
            // becomes the split key.

            BTREE_ASSERT(inner->slotuse + 1 < innerslotmax);

            inner_node *splitinner = static_cast<inner_node*>(*splitnode);

            // move the split key and it's datum into the left node
            inner->slotkey[inner->slotuse] = *splitkey;
            inner->childid[inner->slotuse + 1] = splitinner->childid[0];
            inner->slotuse++;

            // set new split key and move corresponding datum into right node
            splitinner->childid[0] = newchild;
            *splitkey = newkey;

            return r;
          } else if (slot >= inner->slotuse + 1) {
            // in case the insert slot is in the newly create split
            // node, we reuse the code below.

            slot -= inner->slotuse + 1;
            inner = static_cast<inner_node*>(*splitnode);
            BTREE_PRINT(
                "btree::insert_descend switching to splitted node " << inner << " slot " << slot);
          }
        }

        // move items and put pointer to child node into correct slot
        BTREE_ASSERT(slot >= 0 && slot <= inner->slotuse);

        std::copy_backward(inner->slotkey + slot,
                           inner->slotkey + inner->slotuse,
                           inner->slotkey + inner->slotuse + 1);
        std::copy_backward(inner->childid + slot,
                           inner->childid + inner->slotuse + 1,
                           inner->childid + inner->slotuse + 2);

        inner->slotkey[slot] = newkey;
        inner->childid[slot + 1] = newchild;
        inner->slotuse++;
      }

      return r;
    } else  // n->isleafnode() == true
    {
      leaf_node *leaf = static_cast<leaf_node*>(n);

      int slot = find_lower(leaf, key);

      if (!allow_duplicates && slot < leaf->slotuse
          && key_equal(key, leaf->slotkey[slot])) {
        return std::pair<iterator, bool>(iterator(leaf, slot), false);
      }

      if (leaf->isfull()) {
        split_leaf_node(leaf, splitkey, splitnode);

        // check if insert slot is in the split sibling node
        if (slot >= leaf->slotuse) {
          slot -= leaf->slotuse;
          leaf = static_cast<leaf_node*>(*splitnode);
        }
      }

      // move items and put data item into correct data slot
      BTREE_ASSERT(slot >= 0 && slot <= leaf->slotuse);

      std::copy_backward(leaf->slotkey + slot, leaf->slotkey + leaf->slotuse,
                         leaf->slotkey + leaf->slotuse + 1);
      data_copy_backward(leaf->slotdata + slot, leaf->slotdata + leaf->slotuse,
                         leaf->slotdata + leaf->slotuse + 1);

      leaf->slotkey[slot] = key;
      if (!used_as_set)
        leaf->slotdata[slot] = value;
      leaf->slotuse++;

      if (splitnode && leaf != *splitnode && slot == leaf->slotuse - 1) {
        // special case: the node was split, and the insert is at the
        // last slot of the old node. then the splitkey must be
        // updated.
        *splitkey = key;
      }

      return std::pair<iterator, bool>(iterator(leaf, slot), true);
    }
  }

/// Split up a leaf node into two equally-filled sibling leaves. Returns
/// the new nodes and it's insertion key in the two parameters.
  void split_leaf_node(leaf_node* leaf, key_type* _newkey, node** _newleaf) {
    BTREE_ASSERT(leaf->isfull());

    unsigned int mid = (leaf->slotuse >> 1);

    BTREE_PRINT("btree::split_leaf_node on " << leaf);

    leaf_node *newleaf = allocate_leaf();

    newleaf->slotuse = leaf->slotuse - mid;

    newleaf->nextleaf = leaf->nextleaf;
    if (newleaf->nextleaf == NULL) {
      BTREE_ASSERT(leaf == m_tailleaf);
      m_tailleaf = newleaf;
    } else {
      newleaf->nextleaf->prevleaf = newleaf;
    }

    std::copy(leaf->slotkey + mid, leaf->slotkey + leaf->slotuse,
              newleaf->slotkey);
    data_copy(leaf->slotdata + mid, leaf->slotdata + leaf->slotuse,
              newleaf->slotdata);

    leaf->slotuse = mid;
    leaf->nextleaf = newleaf;
    newleaf->prevleaf = leaf;

    *_newkey = leaf->slotkey[leaf->slotuse - 1];
    *_newleaf = newleaf;
  }

/// Split up an inner node into two equally-filled sibling nodes. Returns
/// the new nodes and it's insertion key in the two parameters. Requires
/// the slot of the item will be inserted, so the nodes will be the same
/// size after the insert.
  void split_inner_node(inner_node* inner, key_type* _newkey, node** _newinner,
                        unsigned int addslot) {
    BTREE_ASSERT(inner->isfull());

    unsigned int mid = (inner->slotuse >> 1);

    BTREE_PRINT("btree::split_inner: mid " << mid << " addslot " << addslot);

    // if the split is uneven and the overflowing item will be put into the
    // larger node, then the smaller split node may underflow
    if (addslot <= mid && mid > inner->slotuse - (mid + 1))
      mid--;

    BTREE_PRINT("btree::split_inner: mid " << mid << " addslot " << addslot);

    BTREE_PRINT(
        "btree::split_inner_node on " << inner << " into two nodes " << mid << " and " << inner->slotuse - (mid + 1) << " sized");

    inner_node *newinner = allocate_inner(inner->level);

    newinner->slotuse = inner->slotuse - (mid + 1);

    std::copy(inner->slotkey + mid + 1, inner->slotkey + inner->slotuse,
              newinner->slotkey);
    std::copy(inner->childid + mid + 1, inner->childid + inner->slotuse + 1,
              newinner->childid);

    inner->slotuse = mid;

    *_newkey = inner->slotkey[mid];
    *_newinner = newinner;
  }

 public:

// *** Bulk Loader - Construct Tree from Sorted Sequence

/// Bulk load a sorted range. Loads items into leaves and constructs a
/// B-tree above them. The tree must be empty when calling this function.
  template<typename Iterator>
  void bulk_load(Iterator ibegin, Iterator iend) {
    BTREE_ASSERT(empty());

    m_stats->itemcount = iend - ibegin;

    // calculate number of leaves needed, round up.
    size_t num_items = iend - ibegin;
    size_t num_leaves = (num_items + leafslotmax - 1) / leafslotmax;

    BTREE_PRINT(
        "btree::bulk_load, level 0: " << m_stats->itemcount << " items into " << num_leaves << " leaves with up to " << ((iend - ibegin + num_leaves-1) / num_leaves) << " items per leaf.");

    Iterator it = ibegin;
    for (size_t i = 0; i < num_leaves; ++i) {
      // allocate new leaf node
      leaf_node* leaf = allocate_leaf();

      // copy keys or (key,value) pairs into leaf nodes, uses template
      // switch leaf->set_slot().
      leaf->slotuse = static_cast<int>(num_items / (num_leaves - i));
      for (size_t s = 0; s < leaf->slotuse; ++s, ++it)
        leaf->set_slot(s, *it);

      if (m_tailleaf != NULL) {
        m_tailleaf->nextleaf = leaf;
        leaf->prevleaf = m_tailleaf;
      } else {
        m_headleaf = leaf;
      }
      m_tailleaf = leaf;

      num_items -= leaf->slotuse;
    }

    BTREE_ASSERT(it == iend && num_items == 0);

    // if the btree is so small to fit into one leaf, then we're done.
    if (m_headleaf == m_tailleaf) {
      (*m_root) = m_headleaf;
      return;
    }

    BTREE_ASSERT(m_stats->leaves == num_leaves);

    // create first level of inner nodes, pointing to the leaves.
    size_t num_parents = (num_leaves + (innerslotmax + 1) - 1)
        / (innerslotmax + 1);

    BTREE_PRINT(
        "btree::bulk_load, level 1: " << num_leaves << " leaves in " << num_parents << " inner nodes with up to " << ((num_leaves + num_parents-1) / num_parents) << " leaves per inner node.");

    // save inner nodes and maxkey for next level.
    typedef std::pair<inner_node*, const key_type*> nextlevel_type;
    nextlevel_type* nextlevel = new nextlevel_type[num_parents];
    if (persist)
      pmemalloc_activate(nextlevel);
    else
      pmemalloc_count(nextlevel);

    leaf_node* leaf = m_headleaf;
    for (size_t i = 0; i < num_parents; ++i) {
      // allocate new inner node at level 1
      inner_node* n = allocate_inner(1);

      n->slotuse = static_cast<int>(num_leaves / (num_parents - i));
      BTREE_ASSERT(n->slotuse > 0);
      --n->slotuse;  // this counts keys, but an inner node has keys+1 children.

      // copy last key from each leaf and set child
      for (unsigned short s = 0; s < n->slotuse; ++s) {
        n->slotkey[s] = leaf->slotkey[leaf->slotuse - 1];
        n->childid[s] = leaf;
        leaf = leaf->nextleaf;
      }
      n->childid[n->slotuse] = leaf;

      // track max key of any descendant.
      nextlevel[i].first = n;
      nextlevel[i].second = &leaf->slotkey[leaf->slotuse - 1];

      leaf = leaf->nextleaf;
      num_leaves -= n->slotuse + 1;
    }

    BTREE_ASSERT(leaf == NULL && num_leaves == 0);

    // recursively build inner nodes pointing to inner nodes.
    for (int level = 2; num_parents != 1; ++level) {
      size_t num_children = num_parents;
      num_parents = (num_children + (innerslotmax + 1) - 1)
          / (innerslotmax + 1);

      BTREE_PRINT(
          "btree::bulk_load, level " << level << ": " << num_children << " children in " << num_parents << " inner nodes with up to " << ((num_children + num_parents-1) / num_parents) << " children per inner node.");

      size_t inner_index = 0;
      for (size_t i = 0; i < num_parents; ++i) {
        // allocate new inner node at level
        inner_node* n = allocate_inner(level);

        n->slotuse = static_cast<int>(num_children / (num_parents - i));
        BTREE_ASSERT(n->slotuse > 0);
        --n->slotuse;  // this counts keys, but an inner node has keys+1 children.

        // copy children and maxkeys from nextlevel
        for (unsigned short s = 0; s < n->slotuse; ++s) {
          n->slotkey[s] = *nextlevel[inner_index].second;
          n->childid[s] = nextlevel[inner_index].first;
          ++inner_index;
        }
        n->childid[n->slotuse] = nextlevel[inner_index].first;

        // reuse nextlevel array for parents, because we can overwrite
        // slots we've already consumed.
        nextlevel[i].first = n;
        nextlevel[i].second = nextlevel[inner_index].second;

        ++inner_index;
        num_children -= n->slotuse + 1;
      }

      BTREE_ASSERT(num_children == 0);
    }

    (*m_root) = nextlevel[0].first;
    delete[] nextlevel;

    if (selfverify)
      verify();
  }

 private:
// *** Support Class Encapsulating Deletion Results

/// Result flags of recursive deletion.
  enum result_flags_t {
    /// Deletion successful and no fix-ups necessary.
    btree_ok = 0,

    /// Deletion not successful because key was not found.
    btree_not_found = 1,

    /// Deletion successful, the last key was updated so parent slotkeys
    /// need updates.
    btree_update_lastkey = 2,

    /// Deletion successful, children nodes were merged and the parent
    /// needs to remove the empty node.
    btree_fixmerge = 4
  };

/// B+ tree recursive deletion has much information which is needs to be
/// passed upward.
  struct result_t {
    /// Merged result flags
    result_flags_t flags;

    /// The key to be updated at the parent's slot
    key_type lastkey;

    /// Constructor of a result with a specific flag, this can also be used
    /// as for implicit conversion.
    inline result_t(result_flags_t f = btree_ok)
        : flags(f),
          lastkey() {
    }

    /// Constructor with a lastkey value.
    inline result_t(result_flags_t f, const key_type &k)
        : flags(f),
          lastkey(k) {
    }

    /// Test if this result object has a given flag set.
    inline bool has(result_flags_t f) const {
      return (flags & f) != 0;
    }

    /// Merge two results OR-ing the result flags and overwriting lastkeys.
    inline result_t& operator|=(const result_t &other) {
      flags = result_flags_t(flags | other.flags);

      // we overwrite existing lastkeys on purpose
      if (other.has(btree_update_lastkey))
        lastkey = other.lastkey;

      return *this;
    }
  };

 public:
// *** Public Erase Functions

/// Erases one (the first) of the key/data pairs associated with the given
/// key.
  bool erase_one(const key_type &key) {
    BTREE_PRINT("btree::erase_one(" << key << ") on btree size " << size());

    if (selfverify)
      verify();

    if (!(*m_root))
      return false;

    result_t result = erase_one_descend(key, (*m_root), NULL, NULL, NULL, NULL,
    NULL,
                                        0);

    if (!result.has(btree_not_found))
      --m_stats->itemcount;

#ifdef BTREE_DEBUG
    if (debug) print(std::cout);
#endif
    if (selfverify)
      verify();

    return !result.has(btree_not_found);
  }

/// Erases all the key/data pairs associated with the given key. This is
/// implemented using erase_one().
  size_type erase(const key_type &key) {
    size_type c = 0;

    while (erase_one(key)) {
      ++c;
      if (!allow_duplicates)
        break;
    }

    return c;
  }

/// Erase the key/data pair referenced by the iterator.
  void erase(iterator iter) {
    BTREE_PRINT(
        "btree::erase_iter(" << iter.currnode << "," << iter.currslot << ") on btree size " << size());

    if (selfverify)
      verify();

    if (!(*m_root))
      return;

    result_t result = erase_iter_descend(iter, (*m_root), NULL, NULL, NULL,
    NULL,
                                         NULL, 0);

    if (!result.has(btree_not_found))
      --m_stats->itemcount;

#ifdef BTREE_DEBUG
    if (debug) print(std::cout);
#endif
    if (selfverify)
      verify();
  }

#ifdef BTREE_TODO
/// Erase all key/data pairs in the range [first,last). This function is
/// currently not implemented by the B+ Tree.
  void erase(iterator /* first */, iterator /* last */)
  {
    abort();
  }
#endif

 private:
// *** Private Erase Functions

  /** @brief Erase one (the first) key/data pair in the B+ tree matching key.
   *
   * Descends down the tree in search of key. During the descent the parent,
   * left and right siblings and their parents are computed and passed
   * down. Once the key/data pair is found, it is removed from the leaf. If
   * the leaf underflows 6 different cases are handled. These cases resolve
   * the underflow by shifting key/data pairs from adjacent sibling nodes,
   * merging two sibling nodes or trimming the tree.
   */
  result_t erase_one_descend(const key_type& key, node *curr, node *left,
                             node *right, inner_node *leftparent,
                             inner_node *rightparent, inner_node *parent,
                             unsigned int parentslot) {
    if (curr->isleafnode()) {
      leaf_node *leaf = static_cast<leaf_node*>(curr);
      leaf_node *leftleaf = static_cast<leaf_node*>(left);
      leaf_node *rightleaf = static_cast<leaf_node*>(right);

      int slot = find_lower(leaf, key);

      if (slot >= leaf->slotuse || !key_equal(key, leaf->slotkey[slot])) {
        BTREE_PRINT("Could not find key " << key << " to erase.");

        return btree_not_found;
      }

      BTREE_PRINT("Found key in leaf " << curr << " at slot " << slot);

      std::copy(leaf->slotkey + slot + 1, leaf->slotkey + leaf->slotuse,
                leaf->slotkey + slot);
      data_copy(leaf->slotdata + slot + 1, leaf->slotdata + leaf->slotuse,
                leaf->slotdata + slot);

      leaf->slotuse--;

      result_t myres = btree_ok;

      // if the last key of the leaf was changed, the parent is notified
      // and updates the key of this leaf
      if (slot == leaf->slotuse) {
        if (parent && parentslot < parent->slotuse) {
          BTREE_ASSERT(parent->childid[parentslot] == curr);
          parent->slotkey[parentslot] = leaf->slotkey[leaf->slotuse - 1];
        } else {
          if (leaf->slotuse >= 1) {
            BTREE_PRINT(
                "Scheduling lastkeyupdate: key " << leaf->slotkey[leaf->slotuse - 1]);
            myres |= result_t(btree_update_lastkey,
                              leaf->slotkey[leaf->slotuse - 1]);
          } else {
            BTREE_ASSERT(leaf == (*m_root));
          }
        }
      }

      if (leaf->isunderflow() && !(leaf == (*m_root) && leaf->slotuse >= 1)) {
        // determine what to do about the underflow

        // case : if this empty leaf is the root, then delete all nodes
        // and set root to NULL.
        if (leftleaf == NULL && rightleaf == NULL) {
          BTREE_ASSERT(leaf == (*m_root));BTREE_ASSERT(leaf->slotuse == 0);

          free_node((*m_root));

          (*m_root) = leaf = NULL;
          m_headleaf = m_tailleaf = NULL;

          // will be decremented soon by insert_start()
          BTREE_ASSERT(m_stats->itemcount == 1);BTREE_ASSERT(m_stats->leaves == 0);BTREE_ASSERT(m_stats->innernodes == 0);

          return btree_ok;
        }
        // case : if both left and right leaves would underflow in case of
        // a shift, then merging is necessary. choose the more local merger
        // with our parent
        else if ((leftleaf == NULL || leftleaf->isfew())
            && (rightleaf == NULL || rightleaf->isfew())) {
          if (leftparent == parent)
            myres |= merge_leaves(leftleaf, leaf, leftparent);
          else
            myres |= merge_leaves(leaf, rightleaf, rightparent);
        }
        // case : the right leaf has extra data, so balance right with current
        else if ((leftleaf != NULL && leftleaf->isfew())
            && (rightleaf != NULL && !rightleaf->isfew())) {
          if (rightparent == parent)
            myres |= shift_left_leaf(leaf, rightleaf, rightparent, parentslot);
          else
            myres |= merge_leaves(leftleaf, leaf, leftparent);
        }
        // case : the left leaf has extra data, so balance left with current
        else if ((leftleaf != NULL && !leftleaf->isfew())
            && (rightleaf != NULL && rightleaf->isfew())) {
          if (leftparent == parent)
            shift_right_leaf(leftleaf, leaf, leftparent, parentslot - 1);
          else
            myres |= merge_leaves(leaf, rightleaf, rightparent);
        }
        // case : both the leaf and right leaves have extra data and our
        // parent, choose the leaf with more data
        else if (leftparent == rightparent) {
          if (leftleaf->slotuse <= rightleaf->slotuse)
            myres |= shift_left_leaf(leaf, rightleaf, rightparent, parentslot);
          else
            shift_right_leaf(leftleaf, leaf, leftparent, parentslot - 1);
        } else {
          if (leftparent == parent)
            shift_right_leaf(leftleaf, leaf, leftparent, parentslot - 1);
          else
            myres |= shift_left_leaf(leaf, rightleaf, rightparent, parentslot);
        }
      }

      return myres;
    } else  // !curr->isleafnode()
    {
      inner_node *inner = static_cast<inner_node*>(curr);
      inner_node *leftinner = static_cast<inner_node*>(left);
      inner_node *rightinner = static_cast<inner_node*>(right);

      node *myleft, *myright;
      inner_node *myleftparent, *myrightparent;

      int slot = find_lower(inner, key);

      if (slot == 0) {
        myleft =
            (left == NULL) ?
                NULL :
                (static_cast<inner_node*>(left))->childid[left->slotuse - 1];
        myleftparent = leftparent;
      } else {
        myleft = inner->childid[slot - 1];
        myleftparent = inner;
      }

      if (slot == inner->slotuse) {
        myright =
            (right == NULL) ?
            NULL :
                              (static_cast<inner_node*>(right))->childid[0];
        myrightparent = rightparent;
      } else {
        myright = inner->childid[slot + 1];
        myrightparent = inner;
      }

      BTREE_PRINT("erase_one_descend into " << inner->childid[slot]);

      result_t result = erase_one_descend(key, inner->childid[slot], myleft,
                                          myright, myleftparent, myrightparent,
                                          inner, slot);

      result_t myres = btree_ok;

      if (result.has(btree_not_found)) {
        return result;
      }

      if (result.has(btree_update_lastkey)) {
        if (parent && parentslot < parent->slotuse) {
          BTREE_PRINT(
              "Fixing lastkeyupdate: key " << result.lastkey << " into parent " << parent << " at parentslot " << parentslot);

          BTREE_ASSERT(parent->childid[parentslot] == curr);
          parent->slotkey[parentslot] = result.lastkey;
        } else {
          BTREE_PRINT("Forwarding lastkeyupdate: key " << result.lastkey);
          myres |= result_t(btree_update_lastkey, result.lastkey);
        }
      }

      if (result.has(btree_fixmerge)) {
        // either the current node or the next is empty and should be removed
        if (inner->childid[slot]->slotuse != 0)
          slot++;

        // this is the child slot invalidated by the merge
        BTREE_ASSERT(inner->childid[slot]->slotuse == 0);

        free_node(inner->childid[slot]);

        std::copy(inner->slotkey + slot, inner->slotkey + inner->slotuse,
                  inner->slotkey + slot - 1);
        std::copy(inner->childid + slot + 1,
                  inner->childid + inner->slotuse + 1, inner->childid + slot);

        inner->slotuse--;

        if (inner->level == 1) {
          // fix split key for children leaves
          slot--;
          leaf_node *child = static_cast<leaf_node*>(inner->childid[slot]);
          inner->slotkey[slot] = child->slotkey[child->slotuse - 1];
        }
      }

      if (inner->isunderflow()
          && !(inner == (*m_root) && inner->slotuse >= 1)) {
        // case: the inner node is the root and has just one child. that child becomes the new root
        if (leftinner == NULL && rightinner == NULL) {
          BTREE_ASSERT(inner == (*m_root));BTREE_ASSERT(inner->slotuse == 0);

          (*m_root) = inner->childid[0];

          inner->slotuse = 0;
          free_node(inner);

          return btree_ok;
        }
        // case : if both left and right leaves would underflow in case of
        // a shift, then merging is necessary. choose the more local merger
        // with our parent
        else if ((leftinner == NULL || leftinner->isfew())
            && (rightinner == NULL || rightinner->isfew())) {
          if (leftparent == parent)
            myres |= merge_inner(leftinner, inner, leftparent, parentslot - 1);
          else
            myres |= merge_inner(inner, rightinner, rightparent, parentslot);
        }
        // case : the right leaf has extra data, so balance right with current
        else if ((leftinner != NULL && leftinner->isfew())
            && (rightinner != NULL && !rightinner->isfew())) {
          if (rightparent == parent)
            shift_left_inner(inner, rightinner, rightparent, parentslot);
          else
            myres |= merge_inner(leftinner, inner, leftparent, parentslot - 1);
        }
        // case : the left leaf has extra data, so balance left with current
        else if ((leftinner != NULL && !leftinner->isfew())
            && (rightinner != NULL && rightinner->isfew())) {
          if (leftparent == parent)
            shift_right_inner(leftinner, inner, leftparent, parentslot - 1);
          else
            myres |= merge_inner(inner, rightinner, rightparent, parentslot);
        }
        // case : both the leaf and right leaves have extra data and our
        // parent, choose the leaf with more data
        else if (leftparent == rightparent) {
          if (leftinner->slotuse <= rightinner->slotuse)
            shift_left_inner(inner, rightinner, rightparent, parentslot);
          else
            shift_right_inner(leftinner, inner, leftparent, parentslot - 1);
        } else {
          if (leftparent == parent)
            shift_right_inner(leftinner, inner, leftparent, parentslot - 1);
          else
            shift_left_inner(inner, rightinner, rightparent, parentslot);
        }
      }

      return myres;
    }
  }

  /** @brief Erase one key/data pair referenced by an iterator in the B+
   * tree.
   *
   * Descends down the tree in search of an iterator. During the descent the
   * parent, left and right siblings and their parents are computed and
   * passed down. The difficulty is that the iterator contains only a pointer
   * to a leaf_node, which means that this function must do a recursive depth
   * first search for that leaf node in the subtree containing all pairs of
   * the same key. This subtree can be very large, even the whole tree,
   * though in practice it would not make sense to have so many duplicate
   * keys.
   *
   * Once the referenced key/data pair is found, it is removed from the leaf
   * and the same underflow cases are handled as in erase_one_descend.
   */
  result_t erase_iter_descend(const iterator& iter, node *curr, node *left,
                              node *right, inner_node *leftparent,
                              inner_node *rightparent, inner_node *parent,
                              unsigned int parentslot) {
    if (curr->isleafnode()) {
      leaf_node *leaf = static_cast<leaf_node*>(curr);
      leaf_node *leftleaf = static_cast<leaf_node*>(left);
      leaf_node *rightleaf = static_cast<leaf_node*>(right);

      // if this is not the correct leaf, get next step in recursive
      // search
      if (leaf != iter.currnode) {
        return btree_not_found;
      }

      if (iter.currslot >= leaf->slotuse) {
        BTREE_PRINT(
            "Could not find iterator (" << iter.currnode << "," << iter.currslot << ") to erase. Invalid leaf node?");

        return btree_not_found;
      }

      int slot = iter.currslot;

      BTREE_PRINT("Found iterator in leaf " << curr << " at slot " << slot);

      std::copy(leaf->slotkey + slot + 1, leaf->slotkey + leaf->slotuse,
                leaf->slotkey + slot);
      data_copy(leaf->slotdata + slot + 1, leaf->slotdata + leaf->slotuse,
                leaf->slotdata + slot);

      leaf->slotuse--;

      result_t myres = btree_ok;

      // if the last key of the leaf was changed, the parent is notified
      // and updates the key of this leaf
      if (slot == leaf->slotuse) {
        if (parent && parentslot < parent->slotuse) {
          BTREE_ASSERT(parent->childid[parentslot] == curr);
          parent->slotkey[parentslot] = leaf->slotkey[leaf->slotuse - 1];
        } else {
          if (leaf->slotuse >= 1) {
            BTREE_PRINT(
                "Scheduling lastkeyupdate: key " << leaf->slotkey[leaf->slotuse - 1]);
            myres |= result_t(btree_update_lastkey,
                              leaf->slotkey[leaf->slotuse - 1]);
          } else {
            BTREE_ASSERT(leaf == (*m_root));
          }
        }
      }

      if (leaf->isunderflow() && !(leaf == (*m_root) && leaf->slotuse >= 1)) {
        // determine what to do about the underflow

        // case : if this empty leaf is the root, then delete all nodes
        // and set root to NULL.
        if (leftleaf == NULL && rightleaf == NULL) {
          BTREE_ASSERT(leaf == (*m_root));BTREE_ASSERT(leaf->slotuse == 0);

          free_node((*m_root));

          (*m_root) = leaf = NULL;
          m_headleaf = m_tailleaf = NULL;

          // will be decremented soon by insert_start()
          BTREE_ASSERT(m_stats->itemcount == 1);BTREE_ASSERT(m_stats->leaves == 0);BTREE_ASSERT(m_stats->innernodes == 0);

          return btree_ok;
        }
        // case : if both left and right leaves would underflow in case of
        // a shift, then merging is necessary. choose the more local merger
        // with our parent
        else if ((leftleaf == NULL || leftleaf->isfew())
            && (rightleaf == NULL || rightleaf->isfew())) {
          if (leftparent == parent)
            myres |= merge_leaves(leftleaf, leaf, leftparent);
          else
            myres |= merge_leaves(leaf, rightleaf, rightparent);
        }
        // case : the right leaf has extra data, so balance right with current
        else if ((leftleaf != NULL && leftleaf->isfew())
            && (rightleaf != NULL && !rightleaf->isfew())) {
          if (rightparent == parent)
            myres |= shift_left_leaf(leaf, rightleaf, rightparent, parentslot);
          else
            myres |= merge_leaves(leftleaf, leaf, leftparent);
        }
        // case : the left leaf has extra data, so balance left with current
        else if ((leftleaf != NULL && !leftleaf->isfew())
            && (rightleaf != NULL && rightleaf->isfew())) {
          if (leftparent == parent)
            shift_right_leaf(leftleaf, leaf, leftparent, parentslot - 1);
          else
            myres |= merge_leaves(leaf, rightleaf, rightparent);
        }
        // case : both the leaf and right leaves have extra data and our
        // parent, choose the leaf with more data
        else if (leftparent == rightparent) {
          if (leftleaf->slotuse <= rightleaf->slotuse)
            myres |= shift_left_leaf(leaf, rightleaf, rightparent, parentslot);
          else
            shift_right_leaf(leftleaf, leaf, leftparent, parentslot - 1);
        } else {
          if (leftparent == parent)
            shift_right_leaf(leftleaf, leaf, leftparent, parentslot - 1);
          else
            myres |= shift_left_leaf(leaf, rightleaf, rightparent, parentslot);
        }
      }

      return myres;
    } else  // !curr->isleafnode()
    {
      inner_node *inner = static_cast<inner_node*>(curr);
      inner_node *leftinner = static_cast<inner_node*>(left);
      inner_node *rightinner = static_cast<inner_node*>(right);

      // find first slot below which the searched iterator might be
      // located.

      result_t result;
      int slot = find_lower(inner, iter.key());

      while (slot <= inner->slotuse) {
        node *myleft, *myright;
        inner_node *myleftparent, *myrightparent;

        if (slot == 0) {
          myleft =
              (left == NULL) ?
                  NULL :
                  (static_cast<inner_node*>(left))->childid[left->slotuse - 1];
          myleftparent = leftparent;
        } else {
          myleft = inner->childid[slot - 1];
          myleftparent = inner;
        }

        if (slot == inner->slotuse) {
          myright =
              (right == NULL) ?
              NULL :
                                (static_cast<inner_node*>(right))->childid[0];
          myrightparent = rightparent;
        } else {
          myright = inner->childid[slot + 1];
          myrightparent = inner;
        }

        BTREE_PRINT("erase_iter_descend into " << inner->childid[slot]);

        result = erase_iter_descend(iter, inner->childid[slot], myleft, myright,
                                    myleftparent, myrightparent, inner, slot);

        if (!result.has(btree_not_found))
          break;

        // continue recursive search for leaf on next slot

        if (slot < inner->slotuse && key_less(inner->slotkey[slot], iter.key()))
          return btree_not_found;

        ++slot;
      }

      if (slot > inner->slotuse)
        return btree_not_found;

      result_t myres = btree_ok;

      if (result.has(btree_update_lastkey)) {
        if (parent && parentslot < parent->slotuse) {
          BTREE_PRINT(
              "Fixing lastkeyupdate: key " << result.lastkey << " into parent " << parent << " at parentslot " << parentslot);

          BTREE_ASSERT(parent->childid[parentslot] == curr);
          parent->slotkey[parentslot] = result.lastkey;
        } else {
          BTREE_PRINT("Forwarding lastkeyupdate: key " << result.lastkey);
          myres |= result_t(btree_update_lastkey, result.lastkey);
        }
      }

      if (result.has(btree_fixmerge)) {
        // either the current node or the next is empty and should be removed
        if (inner->childid[slot]->slotuse != 0)
          slot++;

        // this is the child slot invalidated by the merge
        BTREE_ASSERT(inner->childid[slot]->slotuse == 0);

        free_node(inner->childid[slot]);

        std::copy(inner->slotkey + slot, inner->slotkey + inner->slotuse,
                  inner->slotkey + slot - 1);
        std::copy(inner->childid + slot + 1,
                  inner->childid + inner->slotuse + 1, inner->childid + slot);

        inner->slotuse--;

        if (inner->level == 1) {
          // fix split key for children leaves
          slot--;
          leaf_node *child = static_cast<leaf_node*>(inner->childid[slot]);
          inner->slotkey[slot] = child->slotkey[child->slotuse - 1];
        }
      }

      if (inner->isunderflow()
          && !(inner == (*m_root) && inner->slotuse >= 1)) {
        // case: the inner node is the root and has just one
        // child. that child becomes the new root
        if (leftinner == NULL && rightinner == NULL) {
          BTREE_ASSERT(inner == (*m_root));BTREE_ASSERT(inner->slotuse == 0);

          (*m_root) = inner->childid[0];

          inner->slotuse = 0;
          free_node(inner);

          return btree_ok;
        }
        // case : if both left and right leaves would underflow in case of
        // a shift, then merging is necessary. choose the more local merger
        // with our parent
        else if ((leftinner == NULL || leftinner->isfew())
            && (rightinner == NULL || rightinner->isfew())) {
          if (leftparent == parent)
            myres |= merge_inner(leftinner, inner, leftparent, parentslot - 1);
          else
            myres |= merge_inner(inner, rightinner, rightparent, parentslot);
        }
        // case : the right leaf has extra data, so balance right with current
        else if ((leftinner != NULL && leftinner->isfew())
            && (rightinner != NULL && !rightinner->isfew())) {
          if (rightparent == parent)
            shift_left_inner(inner, rightinner, rightparent, parentslot);
          else
            myres |= merge_inner(leftinner, inner, leftparent, parentslot - 1);
        }
        // case : the left leaf has extra data, so balance left with current
        else if ((leftinner != NULL && !leftinner->isfew())
            && (rightinner != NULL && rightinner->isfew())) {
          if (leftparent == parent)
            shift_right_inner(leftinner, inner, leftparent, parentslot - 1);
          else
            myres |= merge_inner(inner, rightinner, rightparent, parentslot);
        }
        // case : both the leaf and right leaves have extra data and our
        // parent, choose the leaf with more data
        else if (leftparent == rightparent) {
          if (leftinner->slotuse <= rightinner->slotuse)
            shift_left_inner(inner, rightinner, rightparent, parentslot);
          else
            shift_right_inner(leftinner, inner, leftparent, parentslot - 1);
        } else {
          if (leftparent == parent)
            shift_right_inner(leftinner, inner, leftparent, parentslot - 1);
          else
            shift_left_inner(inner, rightinner, rightparent, parentslot);
        }
      }

      return myres;
    }
  }

/// Merge two leaf nodes. The function moves all key/data pairs from right
/// to left and sets right's slotuse to zero. The right slot is then
/// removed by the calling parent node.
  result_t merge_leaves(leaf_node* left, leaf_node* right, inner_node* parent) {
    BTREE_PRINT(
        "Merge leaf nodes " << left << " and " << right << " with common parent " << parent << ".");
    (void) parent;

    BTREE_ASSERT(left->isleafnode() && right->isleafnode());BTREE_ASSERT(parent->level == 1);

    BTREE_ASSERT(left->slotuse + right->slotuse < leafslotmax);

    std::copy(right->slotkey, right->slotkey + right->slotuse,
              left->slotkey + left->slotuse);
    data_copy(right->slotdata, right->slotdata + right->slotuse,
              left->slotdata + left->slotuse);

    left->slotuse += right->slotuse;

    left->nextleaf = right->nextleaf;
    if (left->nextleaf)
      left->nextleaf->prevleaf = left;
    else
      m_tailleaf = left;

    right->slotuse = 0;

    return btree_fixmerge;
  }

/// Merge two inner nodes. The function moves all key/childid pairs from
/// right to left and sets right's slotuse to zero. The right slot is then
/// removed by the calling parent node.
  static result_t merge_inner(inner_node* left, inner_node* right,
                              inner_node* parent, unsigned int parentslot) {
    BTREE_PRINT(
        "Merge inner nodes " << left << " and " << right << " with common parent " << parent << ".");

    BTREE_ASSERT(left->level == right->level);BTREE_ASSERT(parent->level == left->level + 1);

    BTREE_ASSERT(parent->childid[parentslot] == left);

    BTREE_ASSERT(left->slotuse + right->slotuse < innerslotmax);

    if (selfverify) {
      // find the left node's slot in the parent's children
      unsigned int leftslot = 0;
      while (leftslot <= parent->slotuse && parent->childid[leftslot] != left)
        ++leftslot;

      BTREE_ASSERT(leftslot < parent->slotuse);BTREE_ASSERT(parent->childid[leftslot] == left);BTREE_ASSERT(parent->childid[leftslot+1] == right);

      BTREE_ASSERT(parentslot == leftslot);
    }

    // retrieve the decision key from parent
    left->slotkey[left->slotuse] = parent->slotkey[parentslot];
    left->slotuse++;

    // copy over keys and children from right
    std::copy(right->slotkey, right->slotkey + right->slotuse,
              left->slotkey + left->slotuse);
    std::copy(right->childid, right->childid + right->slotuse + 1,
              left->childid + left->slotuse);

    left->slotuse += right->slotuse;
    right->slotuse = 0;

    return btree_fixmerge;
  }

/// Balance two leaf nodes. The function moves key/data pairs from right to
/// left so that both nodes are equally filled. The parent node is updated
/// if possible.
  static result_t shift_left_leaf(leaf_node *left, leaf_node *right,
                                  inner_node *parent, unsigned int parentslot) {
    BTREE_ASSERT(left->isleafnode() && right->isleafnode());BTREE_ASSERT(parent->level == 1);

    BTREE_ASSERT(left->nextleaf == right);BTREE_ASSERT(left == right->prevleaf);

    BTREE_ASSERT(left->slotuse < right->slotuse);BTREE_ASSERT(parent->childid[parentslot] == left);

    unsigned int shiftnum = (right->slotuse - left->slotuse) >> 1;

    BTREE_PRINT(
        "Shifting (leaf) " << shiftnum << " entries to left " << left << " from right " << right << " with common parent " << parent << ".");

    BTREE_ASSERT(left->slotuse + shiftnum < leafslotmax);

    // copy the first items from the right node to the last slot in the left node.

    std::copy(right->slotkey, right->slotkey + shiftnum,
              left->slotkey + left->slotuse);
    data_copy(right->slotdata, right->slotdata + shiftnum,
              left->slotdata + left->slotuse);

    left->slotuse += shiftnum;

    // shift all slots in the right node to the left

    std::copy(right->slotkey + shiftnum, right->slotkey + right->slotuse,
              right->slotkey);
    data_copy(right->slotdata + shiftnum, right->slotdata + right->slotuse,
              right->slotdata);

    right->slotuse -= shiftnum;

    // fixup parent
    if (parentslot < parent->slotuse) {
      parent->slotkey[parentslot] = left->slotkey[left->slotuse - 1];
      return btree_ok;
    } else {  // the update is further up the tree
      return result_t(btree_update_lastkey, left->slotkey[left->slotuse - 1]);
    }
  }

/// Balance two inner nodes. The function moves key/data pairs from right
/// to left so that both nodes are equally filled. The parent node is
/// updated if possible.
  static void shift_left_inner(inner_node *left, inner_node *right,
                               inner_node *parent, unsigned int parentslot) {
    BTREE_ASSERT(left->level == right->level);BTREE_ASSERT(parent->level == left->level + 1);

    BTREE_ASSERT(left->slotuse < right->slotuse);BTREE_ASSERT(parent->childid[parentslot] == left);

    unsigned int shiftnum = (right->slotuse - left->slotuse) >> 1;

    BTREE_PRINT(
        "Shifting (inner) " << shiftnum << " entries to left " << left << " from right " << right << " with common parent " << parent << ".");

    BTREE_ASSERT(left->slotuse + shiftnum < innerslotmax);

    if (selfverify) {
      // find the left node's slot in the parent's children and compare to parentslot

      unsigned int leftslot = 0;
      while (leftslot <= parent->slotuse && parent->childid[leftslot] != left)
        ++leftslot;

      BTREE_ASSERT(leftslot < parent->slotuse);BTREE_ASSERT(parent->childid[leftslot] == left);BTREE_ASSERT(parent->childid[leftslot+1] == right);

      BTREE_ASSERT(leftslot == parentslot);
    }

    // copy the parent's decision slotkey and childid to the first new key on the left
    left->slotkey[left->slotuse] = parent->slotkey[parentslot];
    left->slotuse++;

    // copy the other items from the right node to the last slots in the left node.

    std::copy(right->slotkey, right->slotkey + shiftnum - 1,
              left->slotkey + left->slotuse);
    std::copy(right->childid, right->childid + shiftnum,
              left->childid + left->slotuse);

    left->slotuse += shiftnum - 1;

    // fixup parent
    parent->slotkey[parentslot] = right->slotkey[shiftnum - 1];

    // shift all slots in the right node

    std::copy(right->slotkey + shiftnum, right->slotkey + right->slotuse,
              right->slotkey);
    std::copy(right->childid + shiftnum, right->childid + right->slotuse + 1,
              right->childid);

    right->slotuse -= shiftnum;
  }

/// Balance two leaf nodes. The function moves key/data pairs from left to
/// right so that both nodes are equally filled. The parent node is updated
/// if possible.
  static void shift_right_leaf(leaf_node *left, leaf_node *right,
                               inner_node *parent, unsigned int parentslot) {
    BTREE_ASSERT(left->isleafnode() && right->isleafnode());BTREE_ASSERT(parent->level == 1);

    BTREE_ASSERT(left->nextleaf == right);BTREE_ASSERT(left == right->prevleaf);BTREE_ASSERT(parent->childid[parentslot] == left);

    BTREE_ASSERT(left->slotuse > right->slotuse);

    unsigned int shiftnum = (left->slotuse - right->slotuse) >> 1;

    BTREE_PRINT(
        "Shifting (leaf) " << shiftnum << " entries to right " << right << " from left " << left << " with common parent " << parent << ".");

    if (selfverify) {
      // find the left node's slot in the parent's children
      unsigned int leftslot = 0;
      while (leftslot <= parent->slotuse && parent->childid[leftslot] != left)
        ++leftslot;

      BTREE_ASSERT(leftslot < parent->slotuse);BTREE_ASSERT(parent->childid[leftslot] == left);BTREE_ASSERT(parent->childid[leftslot+1] == right);

      BTREE_ASSERT(leftslot == parentslot);
    }

    // shift all slots in the right node

    BTREE_ASSERT(right->slotuse + shiftnum < leafslotmax);

    std::copy_backward(right->slotkey, right->slotkey + right->slotuse,
                       right->slotkey + right->slotuse + shiftnum);
    data_copy_backward(right->slotdata, right->slotdata + right->slotuse,
                       right->slotdata + right->slotuse + shiftnum);

    right->slotuse += shiftnum;

    // copy the last items from the left node to the first slot in the right node.
    std::copy(left->slotkey + left->slotuse - shiftnum,
              left->slotkey + left->slotuse, right->slotkey);
    data_copy(left->slotdata + left->slotuse - shiftnum,
              left->slotdata + left->slotuse, right->slotdata);

    left->slotuse -= shiftnum;

    parent->slotkey[parentslot] = left->slotkey[left->slotuse - 1];
  }

/// Balance two inner nodes. The function moves key/data pairs from left to
/// right so that both nodes are equally filled. The parent node is updated
/// if possible.
  static void shift_right_inner(inner_node *left, inner_node *right,
                                inner_node *parent, unsigned int parentslot) {
    BTREE_ASSERT(left->level == right->level);BTREE_ASSERT(parent->level == left->level + 1);

    BTREE_ASSERT(left->slotuse > right->slotuse);BTREE_ASSERT(parent->childid[parentslot] == left);

    unsigned int shiftnum = (left->slotuse - right->slotuse) >> 1;

    BTREE_PRINT(
        "Shifting (leaf) " << shiftnum << " entries to right " << right << " from left " << left << " with common parent " << parent << ".");

    if (selfverify) {
      // find the left node's slot in the parent's children
      unsigned int leftslot = 0;
      while (leftslot <= parent->slotuse && parent->childid[leftslot] != left)
        ++leftslot;

      BTREE_ASSERT(leftslot < parent->slotuse);BTREE_ASSERT(parent->childid[leftslot] == left);BTREE_ASSERT(parent->childid[leftslot+1] == right);

      BTREE_ASSERT(leftslot == parentslot);
    }

    // shift all slots in the right node

    BTREE_ASSERT(right->slotuse + shiftnum < innerslotmax);

    std::copy_backward(right->slotkey, right->slotkey + right->slotuse,
                       right->slotkey + right->slotuse + shiftnum);
    std::copy_backward(right->childid, right->childid + right->slotuse + 1,
                       right->childid + right->slotuse + 1 + shiftnum);

    right->slotuse += shiftnum;

    // copy the parent's decision slotkey and childid to the last new key on the right
    right->slotkey[shiftnum - 1] = parent->slotkey[parentslot];

    // copy the remaining last items from the left node to the first slot in the right node.
    std::copy(left->slotkey + left->slotuse - shiftnum + 1,
              left->slotkey + left->slotuse, right->slotkey);
    std::copy(left->childid + left->slotuse - shiftnum + 1,
              left->childid + left->slotuse + 1, right->childid);

    // copy the first to-be-removed key from the left node to the parent's decision slot
    parent->slotkey[parentslot] = left->slotkey[left->slotuse - shiftnum];

    left->slotuse -= shiftnum;
  }

 public:
// *** Verification of B+ Tree Invariants

/// Run a thorough verification of all B+ tree invariants. The program
/// aborts via assert() if something is wrong.
  void verify() const {
    key_type minkey, maxkey;
    tree_stats vstats;

    if ((*m_root)) {
      verify_node((*m_root), &minkey, &maxkey, vstats);

      assert(vstats.itemcount == m_stats->itemcount);
      assert(vstats.leaves == m_stats->leaves);
      assert(vstats.innernodes == m_stats->innernodes);

      verify_leaflinks();
    }
  }

 private:

/// Recursively descend down the tree and verify each node
  void verify_node(const node* n, key_type* minkey, key_type* maxkey,
                   tree_stats &vstats) const {
    BTREE_PRINT("verifynode " << n);

    if (n->isleafnode()) {
      const leaf_node *leaf = static_cast<const leaf_node*>(n);

      assert(leaf == (*m_root) || !leaf->isunderflow());
      assert(leaf->slotuse > 0);

      for (unsigned short slot = 0; slot < leaf->slotuse - 1; ++slot) {
        assert(key_lessequal(leaf->slotkey[slot], leaf->slotkey[slot + 1]));
      }

      *minkey = leaf->slotkey[0];
      *maxkey = leaf->slotkey[leaf->slotuse - 1];

      vstats.leaves++;
      vstats.itemcount += leaf->slotuse;
    } else  // !n->isleafnode()
    {
      const inner_node *inner = static_cast<const inner_node*>(n);
      vstats.innernodes++;

      assert(inner == (*m_root) || !inner->isunderflow());
      assert(inner->slotuse > 0);

      for (unsigned short slot = 0; slot < inner->slotuse - 1; ++slot) {
        assert(key_lessequal(inner->slotkey[slot], inner->slotkey[slot + 1]));
      }

      for (unsigned short slot = 0; slot <= inner->slotuse; ++slot) {
        const node *subnode = inner->childid[slot];
        key_type subminkey = key_type();
        key_type submaxkey = key_type();

        assert(subnode->level + 1 == inner->level);
        verify_node(subnode, &subminkey, &submaxkey, vstats);

        BTREE_PRINT(
            "verify subnode " << subnode << ": " << subminkey << " - " << submaxkey);

        if (slot == 0)
          *minkey = subminkey;
        else
          assert(key_greaterequal(subminkey, inner->slotkey[slot - 1]));

        if (slot == inner->slotuse)
          *maxkey = submaxkey;
        else
          assert(key_equal(inner->slotkey[slot], submaxkey));

        if (inner->level == 1 && slot < inner->slotuse) {
          // children are leaves and must be linked together in the
          // correct order
          const leaf_node *leafa =
              static_cast<const leaf_node*>(inner->childid[slot]);
          const leaf_node *leafb =
              static_cast<const leaf_node*>(inner->childid[slot + 1]);

          assert(leafa->nextleaf == leafb);
          assert(leafa == leafb->prevleaf);
          (void) leafa;
          (void) leafb;
        }
        if (inner->level == 2 && slot < inner->slotuse) {
          // verify leaf links between the adjacent inner nodes
          const inner_node *parenta = static_cast<const inner_node*>(inner
              ->childid[slot]);
          const inner_node *parentb = static_cast<const inner_node*>(inner
              ->childid[slot + 1]);

          const leaf_node *leafa = static_cast<const leaf_node*>(parenta
              ->childid[parenta->slotuse]);
          const leaf_node *leafb = static_cast<const leaf_node*>(parentb
              ->childid[0]);

          assert(leafa->nextleaf == leafb);
          assert(leafa == leafb->prevleaf);
          (void) leafa;
          (void) leafb;
        }
      }
    }
  }

/// Verify the double linked list of leaves.
  void verify_leaflinks() const {
    const leaf_node *n = m_headleaf;

    assert(n->level == 0);
    assert(!n || n->prevleaf == NULL);

    unsigned int testcount = 0;

    while (n) {
      assert(n->level == 0);
      assert(n->slotuse > 0);

      for (unsigned short slot = 0; slot < n->slotuse - 1; ++slot) {
        assert(key_lessequal(n->slotkey[slot], n->slotkey[slot + 1]));
      }

      testcount += n->slotuse;

      if (n->nextleaf) {
        assert(
            key_lessequal(n->slotkey[n->slotuse - 1], n->nextleaf->slotkey[0]));

        assert(n == n->nextleaf->prevleaf);
      } else {
        assert(m_tailleaf == n);
      }

      n = n->nextleaf;
    }

    assert(testcount == size());
  }

 private:
// *** Dump and Restore of B+ Trees

/// A header for the binary image containing the base properties of the B+
/// tree. These properties have to match the current template
/// instantiation.
  struct dump_header {
    /// "stx-btree", just to stop the restore() function from loading garbage
    char signature[12];

    /// Currently 0
    unsigned short version;

    /// sizeof(key_type)
    unsigned short key_type_size;

    /// sizeof(data_type)
    unsigned short data_type_size;

    /// Number of slots in the leaves
    unsigned short leafslots;

    /// Number of slots in the inner nodes
    unsigned short innerslots;

    /// Allow duplicates
    bool allow_duplicates;

    /// The item count of the tree
    size_type itemcount;

    /// Fill the struct with the current B+ tree's properties, itemcount is
    /// not filled.
    inline void fill() {
      // don't want to include string.h just for this signature
      signature[0] = 's';
      signature[1] = 't';
      signature[2] = 'x';
      signature[3] = '-';
      signature[4] = 'b';
      signature[5] = 't';
      signature[6] = 'r';
      signature[7] = 'e';
      signature[8] = 'e';
      signature[9] = 0;
      signature[10] = 0;
      signature[11] = 0;

      version = 0;
      key_type_size = sizeof(typename btree_self::key_type);
      data_type_size = sizeof(typename btree_self::data_type);
      leafslots = btree_self::leafslotmax;
      innerslots = btree_self::innerslotmax;
      allow_duplicates = btree_self::allow_duplicates;
    }

    /// Returns true if the headers have the same vital properties
    inline bool same(const struct dump_header &o) const {
      return (signature[0] == 's' && signature[1] == 't' && signature[2] == 'x'
          && signature[3] == '-' && signature[4] == 'b' && signature[5] == 't'
          && signature[6] == 'r' && signature[7] == 'e' && signature[8] == 'e'
          && signature[9] == 0 && signature[10] == 0 && signature[11] == 0)
          && (version == o.version) && (key_type_size == o.key_type_size)
          && (data_type_size == o.data_type_size) && (leafslots == o.leafslots)
          && (innerslots == o.innerslots)
          && (allow_duplicates == o.allow_duplicates);
    }
  };

 public:

/// Dump the contents of the B+ tree out onto an ostream as a binary
/// image. The image contains memory pointers which will be fixed when the
/// image is restored. For this to work your key_type and data_type must be
/// integral types and contain no pointers or references.
  void dump(std::ostream &os) const {
    struct dump_header header;
    header.fill();
    header.itemcount = size();

    os.write(reinterpret_cast<char*>(&header), sizeof(header));

    if ((*m_root)) {
      dump_node(os, (*m_root));
    }
  }

/// Restore a binary image of a dumped B+ tree from an istream. The B+ tree
/// pointers are fixed using the dump order. For dump and restore to work
/// your key_type and data_type must be integral types and contain no
/// pointers or references. Returns true if the restore was successful.
  bool restore(std::istream &is) {
    struct dump_header fileheader;
    is.read(reinterpret_cast<char*>(&fileheader), sizeof(fileheader));
    if (!is.good())
      return false;

    struct dump_header myheader;
    myheader.fill();
    myheader.itemcount = fileheader.itemcount;

    if (!myheader.same(fileheader)) {
      BTREE_PRINT(
          "btree::restore: file header does not match instantiation signature.");
      return false;
    }

    clear();

    if (fileheader.itemcount > 0) {
      (*m_root) = restore_node(is);
      if ((*m_root) == NULL)
        return false;

      m_stats->itemcount = fileheader.itemcount;
    }

    if (selfverify)
      verify();

    return true;
  }

 private:

/// Recursively descend down the tree and dump each node in a precise order
  void dump_node(std::ostream &os, const node* n) const {
    BTREE_PRINT("dump_node " << n << std::endl);

    if (n->isleafnode()) {
      const leaf_node *leaf = static_cast<const leaf_node*>(n);

      os.write(reinterpret_cast<const char*>(leaf), sizeof(*leaf));
    } else  // !n->isleafnode()
    {
      const inner_node *inner = static_cast<const inner_node*>(n);

      os.write(reinterpret_cast<const char*>(inner), sizeof(*inner));

      for (unsigned short slot = 0; slot <= inner->slotuse; ++slot) {
        const node *subnode = inner->childid[slot];

        dump_node(os, subnode);
      }
    }
  }

/// Read the dump image and construct a tree from the node order in the
/// serialization.
  node* restore_node(std::istream &is) {
    union {
      node top;
      leaf_node leaf;
      inner_node inner;
    } nu;

    // first read only the top of the node
    is.read(reinterpret_cast<char*>(&nu.top), sizeof(nu.top));
    if (!is.good())
      return NULL;

    if (nu.top.isleafnode()) {
      // read remaining data of leaf node
      is.read(reinterpret_cast<char*>(&nu.leaf) + sizeof(nu.top),
              sizeof(nu.leaf) - sizeof(nu.top));
      if (!is.good())
        return NULL;

      leaf_node *newleaf = allocate_leaf();

      // copy over all data, the leaf nodes contain only their double linked list pointers
      *newleaf = nu.leaf;

      // reconstruct the linked list from the order in the file
      if (m_headleaf == NULL) {
        BTREE_ASSERT(newleaf->prevleaf == NULL);
        m_headleaf = m_tailleaf = newleaf;
      } else {
        newleaf->prevleaf = m_tailleaf;
        m_tailleaf->nextleaf = newleaf;
        m_tailleaf = newleaf;
      }

      return newleaf;
    } else {
      // read remaining data of inner node
      is.read(reinterpret_cast<char*>(&nu.inner) + sizeof(nu.top),
              sizeof(nu.inner) - sizeof(nu.top));
      if (!is.good())
        return NULL;

      inner_node *newinner = allocate_inner(0);

      // copy over all data, the inner nodes contain only pointers to their children
      *newinner = nu.inner;

      for (unsigned short slot = 0; slot <= newinner->slotuse; ++slot) {
        newinner->childid[slot] = restore_node(is);
      }

      return newinner;
    }
  }
};

/** \file btree_map.h
 * Contains the specialized B+ tree template class btree_map
 */

/** @brief Specialized B+ tree template class implementing STL's map container.
 *
 * Implements the STL map using a B+ tree. It can be used as a drop-in
 * replacement for std::map. Not all asymptotic time requirements are met in
 * theory. The class has a traits class defining B+ tree properties like slots
 * and self-verification. Furthermore an allocator can be specified for tree
 * nodes.
 *
 * Most noteworthy difference to the default red-black implementation of
 * std::map is that the B+ tree does not hold key and data pair together in
 * memory. Instead each B+ tree node has two arrays of keys and data
 * values. This design directly generates many problems in implementing the
 * iterator's operator's which return value_type composition pairs.
 */
template<typename _Key, typename _Data, typename _Compare = std::less<_Key>,
    typename _Traits = btree_default_map_traits<_Key, _Data>,
    typename _Alloc = std::allocator<std::pair<_Key, _Data> > >
class pbtree {
 public:
  // *** Template Parameter Types

  /// First template parameter: The key type of the btree. This is stored in
  /// inner nodes and leaves
  typedef _Key key_type;

  /// Second template parameter: The data type associated with each
  /// key. Stored in the B+ tree's leaves
  typedef _Data data_type;

  /// Third template parameter: Key comparison function object
  typedef _Compare key_compare;

  /// Fourth template parameter: Traits object used to define more parameters
  /// of the B+ tree
  typedef _Traits traits;

  /// Fifth template parameter: STL allocator
  typedef _Alloc allocator_type;

  // The macro BTREE_FRIENDS can be used by outside class to access the B+
  // tree internals. This was added for wxBTreeDemo to be able to draw the
  // tree.
  BTREE_FRIENDS

 public:
  // *** Constructed Types

  /// Typedef of our own type
  typedef pbtree<key_type, data_type, key_compare, traits, allocator_type> self;

  /// Construct the STL-required value_type as a composition pair of key and
  /// data types
  typedef std::pair<key_type, data_type> value_type;

  /// Implementation type of the btree_base
  typedef btree<key_type, data_type, value_type, key_compare, traits, false,
      allocator_type, false> btree_impl;

  /// Function class comparing two value_type pairs.
  typedef typename btree_impl::value_compare value_compare;

  /// Size type used to count keys
  typedef typename btree_impl::size_type size_type;

  /// Small structure containing statistics about the tree
  typedef typename btree_impl::tree_stats tree_stats;

 public:
  // *** Static Constant Options and Values of the B+ Tree

  /// Base B+ tree parameter: The number of key/data slots in each leaf
  static const unsigned short leafslotmax = btree_impl::leafslotmax;

  /// Base B+ tree parameter: The number of key slots in each inner node,
  /// this can differ from slots in each leaf.
  static const unsigned short innerslotmax = btree_impl::innerslotmax;

  /// Computed B+ tree parameter: The minimum number of key/data slots used
  /// in a leaf. If fewer slots are used, the leaf will be merged or slots
  /// shifted from it's siblings.
  static const unsigned short minleafslots = btree_impl::minleafslots;

  /// Computed B+ tree parameter: The minimum number of key slots used
  /// in an inner node. If fewer slots are used, the inner node will be
  /// merged or slots shifted from it's siblings.
  static const unsigned short mininnerslots = btree_impl::mininnerslots;

  /// Debug parameter: Enables expensive and thorough checking of the B+ tree
  /// invariants after each insert/erase operation.
  static const bool selfverify = btree_impl::selfverify;

  /// Debug parameter: Prints out lots of debug information about how the
  /// algorithms change the tree. Requires the header file to be compiled
  /// with BTREE_DEBUG and the key type must be std::ostream printable.
  static const bool debug = btree_impl::debug;

  /// Operational parameter: Allow duplicate keys in the btree.
  static const bool allow_duplicates = btree_impl::allow_duplicates;

 public:
  // *** Iterators and Reverse Iterators

  /// STL-like iterator object for B+ tree items. The iterator points to a
  /// specific slot number in a leaf.
  typedef typename btree_impl::iterator iterator;

  /// STL-like iterator object for B+ tree items. The iterator points to a
  /// specific slot number in a leaf.
  typedef typename btree_impl::const_iterator const_iterator;

  /// create mutable reverse iterator by using STL magic
  typedef typename btree_impl::reverse_iterator reverse_iterator;

  /// create constant reverse iterator by using STL magic
  typedef typename btree_impl::const_reverse_iterator const_reverse_iterator;

 private:
  // *** Tree Implementation Object

  /// The contained implementation object
  btree_impl tree;

 public:
  // *** Constructors and Destructor

  /// Default constructor initializing an empty B+ tree with the standard key
  /// comparison function
  explicit inline pbtree(void** _root, const allocator_type &alloc =
                             allocator_type())
      : tree(_root, alloc) {
  }

  /// Constructor initializing an empty B+ tree with a special key
  /// comparison object
  explicit inline pbtree(const key_compare &kcf, const allocator_type &alloc =
                             allocator_type())
      : tree(kcf, alloc) {
  }

  /// Constructor initializing a B+ tree with the range [first,last)
  template<class InputIterator>
  inline pbtree(InputIterator first, InputIterator last,
                const allocator_type &alloc = allocator_type())
      : tree(first, last, alloc) {
  }

  /// Constructor initializing a B+ tree with the range [first,last) and a
  /// special key comparison object
  template<class InputIterator>
  inline pbtree(InputIterator first, InputIterator last, const key_compare &kcf,
                const allocator_type &alloc = allocator_type())
      : tree(first, last, kcf, alloc) {
  }

  /// Frees up all used B+ tree memory pages
  inline ~pbtree() {
  }

  /// Fast swapping of two identical B+ tree objects.
  void swap(self& from) {
    std::swap(tree, from.tree);
  }

 public:
  // *** Key and Value Comparison Function Objects

  /// Constant access to the key comparison object sorting the B+ tree
  inline key_compare key_comp() const {
    return tree.key_comp();
  }

  /// Constant access to a constructed value_type comparison object. required
  /// by the STL
  inline value_compare value_comp() const {
    return tree.value_comp();
  }

 public:
  // *** Allocators

  /// Return the base node allocator provided during construction.
  allocator_type get_allocator() const {
    return tree.get_allocator();
  }

 public:
  // *** Fast Destruction of the B+ Tree

  /// Frees all key/data pairs and all nodes of the tree
  void clear() {
    tree.clear();
  }

 public:
  // *** STL Iterator Construction Functions

  /// Constructs a read/data-write iterator that points to the first slot in
  /// the first leaf of the B+ tree.
  inline iterator begin() {
    return tree.begin();
  }

  /// Constructs a read/data-write iterator that points to the first invalid
  /// slot in the last leaf of the B+ tree.
  inline iterator end() {
    return tree.end();
  }

  /// Constructs a read-only constant iterator that points to the first slot
  /// in the first leaf of the B+ tree.
  inline const_iterator begin() const {
    return tree.begin();
  }

  /// Constructs a read-only constant iterator that points to the first
  /// invalid slot in the last leaf of the B+ tree.
  inline const_iterator end() const {
    return tree.end();
  }

  /// Constructs a read/data-write reverse iterator that points to the first
  /// invalid slot in the last leaf of the B+ tree. Uses STL magic.
  inline reverse_iterator rbegin() {
    return tree.rbegin();
  }

  /// Constructs a read/data-write reverse iterator that points to the first
  /// slot in the first leaf of the B+ tree. Uses STL magic.
  inline reverse_iterator rend() {
    return tree.rend();
  }

  /// Constructs a read-only reverse iterator that points to the first
  /// invalid slot in the last leaf of the B+ tree. Uses STL magic.
  inline const_reverse_iterator rbegin() const {
    return tree.rbegin();
  }

  /// Constructs a read-only reverse iterator that points to the first slot
  /// in the first leaf of the B+ tree. Uses STL magic.
  inline const_reverse_iterator rend() const {
    return tree.rend();
  }

 public:
  // *** Access Functions to the Item Count

  /// Return the number of key/data pairs in the B+ tree
  inline size_type size() const {
    return tree.size();
  }

  /// Returns true if there is at least one key/data pair in the B+ tree
  inline bool empty() const {
    return tree.empty();
  }

  /// Returns the largest possible size of the B+ Tree. This is just a
  /// function required by the STL standard, the B+ Tree can hold more items.
  inline size_type max_size() const {
    return tree.max_size();
  }

  /// Return a const reference to the current statistics.
  inline const tree_stats& get_stats() const {
    return tree.get_stats();
  }

 public:
  // *** Standard Access Functions Querying the Tree by Descending to a Leaf

  /// Non-STL function checking whether a key is in the B+ tree. The same as
  /// (find(k) != end()) or (count() != 0).
  bool exists(const key_type &key) const {
    return tree.exists(key);
  }

  /// Tries to locate a key in the B+ tree and returns an iterator to the
  /// key/data slot if found. If unsuccessful it returns end().
  iterator find(const key_type &key) {
    return tree.find(key);
  }

  /// Tries to locate a key in the B+ tree and returns an constant iterator
  /// to the key/data slot if found. If unsuccessful it returns end().
  const_iterator find(const key_type &key) const {
    return tree.find(key);
  }

  /// Tries to return value if key is found.
  bool at(const key_type &key, data_type* val) {
    return tree.at(key, val);
  }

  /// Tries to update value if key is found.
  int update(const key_type &key, const data_type &val) {
    return tree.update(key, val);
  }

  // Disable persistence
  void disable_persistence() {
    tree.disable_persistence();
  }

  /// Tries to locate a key in the B+ tree and returns the number of
  /// identical key entries found. Since this is a unique map, count()
  /// returns either 0 or 1.
  size_type count(const key_type &key) const {
    return tree.count(key);
  }

  /// Searches the B+ tree and returns an iterator to the first pair
  /// equal to or greater than key, or end() if all keys are smaller.
  iterator lower_bound(const key_type& key) {
    return tree.lower_bound(key);
  }

  /// Searches the B+ tree and returns a constant iterator to the
  /// first pair equal to or greater than key, or end() if all keys
  /// are smaller.
  const_iterator lower_bound(const key_type& key) const {
    return tree.lower_bound(key);
  }

  /// Searches the B+ tree and returns an iterator to the first pair
  /// greater than key, or end() if all keys are smaller or equal.
  iterator upper_bound(const key_type& key) {
    return tree.upper_bound(key);
  }

  /// Searches the B+ tree and returns a constant iterator to the
  /// first pair greater than key, or end() if all keys are smaller
  /// or equal.
  const_iterator upper_bound(const key_type& key) const {
    return tree.upper_bound(key);
  }

  /// Searches the B+ tree and returns both lower_bound() and upper_bound().
  inline std::pair<iterator, iterator> equal_range(const key_type& key) {
    return tree.equal_range(key);
  }

  /// Searches the B+ tree and returns both lower_bound() and upper_bound().
  inline std::pair<const_iterator, const_iterator> equal_range(
      const key_type& key) const {
    return tree.equal_range(key);
  }

 public:
  // *** B+ Tree Object Comparison Functions

  /// Equality relation of B+ trees of the same type. B+ trees of the same
  /// size and equal elements (both key and data) are considered
  /// equal.
  inline bool operator==(const self &other) const {
    return (tree == other.tree);
  }

  /// Inequality relation. Based on operator==.
  inline bool operator!=(const self &other) const {
    return (tree != other.tree);
  }

  /// Total ordering relation of B+ trees of the same type. It uses
  /// std::lexicographical_compare() for the actual comparison of elements.
  inline bool operator<(const self &other) const {
    return (tree < other.tree);
  }

  /// Greater relation. Based on operator<.
  inline bool operator>(const self &other) const {
    return (tree > other.tree);
  }

  /// Less-equal relation. Based on operator<.
  inline bool operator<=(const self &other) const {
    return (tree <= other.tree);
  }

  /// Greater-equal relation. Based on operator<.
  inline bool operator>=(const self &other) const {
    return (tree >= other.tree);
  }

 public:
  /// *** Fast Copy: Assign Operator and Copy Constructors

  /// Assignment operator. All the key/data pairs are copied
  inline self& operator=(const self &other) {
    if (this != &other) {
      tree = other.tree;
    }
    return *this;
  }

  /// Copy constructor. The newly initialized B+ tree object will contain a
  /// copy of all key/data pairs.
  inline pbtree(const self &other)
      : tree(other.tree) {
  }

 public:
  // *** Public Insertion Functions

  /// Attempt to insert a key/data pair into the B+ tree. Fails if the pair
  /// is already present.
  inline std::pair<iterator, bool> insert(const value_type& x) {
    return tree.insert2(x.first, x.second);
  }

  /// Attempt to insert a key/data pair into the B+ tree. Beware that if
  /// key_type == data_type, then the template iterator insert() is called
  /// instead. Fails if the inserted pair is already present.
  inline std::pair<iterator, bool> insert(const key_type& key,
                                          const data_type& data) {
    return tree.insert2(key, data);
  }

  /// Attempt to insert a key/data pair into the B+ tree. This function is the
  /// same as the other insert, however if key_type == data_type then the
  /// non-template function cannot be called. Fails if the inserted pair is
  /// already present.
  inline std::pair<iterator, bool> insert2(const key_type& key,
                                           const data_type& data) {
    return tree.insert2(key, data);
  }

  /// Attempt to insert a key/data pair into the B+ tree. The iterator hint
  /// is currently ignored by the B+ tree insertion routine.
  inline iterator insert(iterator hint, const value_type &x) {
    return tree.insert2(hint, x.first, x.second);
  }

  /// Attempt to insert a key/data pair into the B+ tree. The iterator hint is
  /// currently ignored by the B+ tree insertion routine.
  inline iterator insert2(iterator hint, const key_type& key,
                          const data_type& data) {
    return tree.insert2(hint, key, data);
  }

  /// Returns a reference to the object that is associated with a particular
  /// key. If the map does not already contain such an object, operator[]
  /// inserts the default object data_type().
  inline data_type& operator[](const key_type& key) {
    iterator i = insert(value_type(key, data_type())).first;
    return i.data();
  }

  /// Attempt to insert the range [first,last) of value_type pairs into the B+
  /// tree. Each key/data pair is inserted individually.
  template<typename InputIterator>
  inline void insert(InputIterator first, InputIterator last) {
    return tree.insert(first, last);
  }

  /// Bulk load a sorted range [first,last). Loads items into leaves and
  /// constructs a B-tree above them. The tree must be empty when calling
  /// this function.
  template<typename Iterator>
  inline void bulk_load(Iterator first, Iterator last) {
    return tree.bulk_load(first, last);
  }

 public:
  // *** Public Erase Functions

  /// Erases the key/data pairs associated with the given key. For this
  /// unique-associative map there is no difference to erase().
  bool erase_one(const key_type &key) {
    return tree.erase_one(key);
  }

  /// Erases all the key/data pairs associated with the given key. This is
  /// implemented using erase_one().
  size_type erase(const key_type &key) {
    return tree.erase(key);
  }

  /// Erase the key/data pair referenced by the iterator.
  void erase(iterator iter) {
  }

 public:
  // *** Verification of B+ Tree Invariants

  /// Run a thorough verification of all B+ tree invariants. The program
  /// aborts via BTREE_ASSERT() if something is wrong.
  void verify() const {
    tree.verify();
  }

 public:

  /// Dump the contents of the B+ tree out onto an ostream as a binary
  /// image. The image contains memory pointers which will be fixed when the
  /// image is restored. For this to work your key_type and data_type must be
  /// integral types and contain no pointers or references.
  void dump(std::ostream &os) const {
    tree.dump(os);
  }

  /// Restore a binary image of a dumped B+ tree from an istream. The B+ tree
  /// pointers are fixed using the dump order. For dump and restore to work
  /// your key_type and data_type must be integral types and contain no
  /// pointers or references. Returns true if the restore was successful.
  bool restore(std::istream &is) {
    return tree.restore(is);
  }
};

#endif /* PBTREE_H_ */
