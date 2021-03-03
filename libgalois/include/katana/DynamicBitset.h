/*
 * This file belongs to the Galois project, a C++ library for exploiting
 * parallelism. The code is being released under the terms of the 3-Clause BSD
 * License (a copy is located in LICENSE.txt at the top-level directory).
 *
 * Copyright (C) 2019, The University of Texas at Austin. All rights reserved.
 * UNIVERSITY EXPRESSLY DISCLAIMS ANY AND ALL WARRANTIES CONCERNING THIS
 * SOFTWARE AND DOCUMENTATION, INCLUDING ANY WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR ANY PARTICULAR PURPOSE, NON-INFRINGEMENT AND WARRANTIES OF
 * PERFORMANCE, AND ANY WARRANTY THAT MIGHT OTHERWISE ARISE FROM COURSE OF
 * DEALING OR USAGE OF TRADE.  NO WARRANTY IS EITHER EXPRESS OR IMPLIED WITH
 * RESPECT TO THE USE OF THE SOFTWARE OR DOCUMENTATION. Under no circumstances
 * shall University be liable for incidental, special, indirect, direct or
 * consequential damages or loss of profits, interruption of business, or
 * related expenses which may arise from use of Software or Documentation,
 * including but not limited to those resulting from defects in Software and/or
 * Documentation, or loss or inaccuracy of data of any kind.
 */

/**
 * @file katana/DynamicBitset.h
 *
 * Contains the DynamicBitset class and most of its implementation.
 */

#ifndef KATANA_LIBGALOIS_KATANA_DYNAMICBITSET_H_
#define KATANA_LIBGALOIS_KATANA_DYNAMICBITSET_H_

#include <cassert>
#include <climits>
#include <vector>

#include <boost/iterator/counting_iterator.hpp>
#include <boost/mpl/has_xxx.hpp>

#include "katana/AtomicWrapper.h"
#include "katana/Galois.h"
#include "katana/PODResizeableArray.h"
#include "katana/config.h"

namespace katana {
/**
 * Concurrent dynamically allocated bitset
 **/
class KATANA_EXPORT DynamicBitset {
  katana::PODResizeableArray<katana::CopyableAtomic<uint64_t>> bitvec;
  size_t num_bits{0};

public:
  static constexpr uint32_t bits_uint64 = sizeof(uint64_t) * CHAR_BIT;

  //! Constructor which initializes to an empty bitset.
  DynamicBitset() = default;

  /**
   * Returns the underlying bitset representation to the user
   *
   * @returns constant reference vector of copyable atomics that represents
   * the bitset
   */
  const auto& get_vec() const { return bitvec; }

  /**
   * Returns the underlying bitset representation to the user
   *
   * @returns reference to vector of copyable atomics that represents the
   * bitset
   */
  auto& get_vec() { return bitvec; }

  /**
   * Resizes the bitset.
   *
   * @param n Size to change the bitset to
   */
  void resize(uint64_t n) {
    KATANA_LOG_DEBUG_ASSERT(
        bits_uint64 == 64);  // compatibility with other devices
    num_bits = n;
    bitvec.resize((n + bits_uint64 - 1) / bits_uint64);
    reset();
  }

  /**
   * Reserves capacity for the bitset.
   *
   * @param n Size to reserve the capacity of the bitset to
   */
  void reserve(uint64_t n) {
    KATANA_LOG_DEBUG_ASSERT(
        bits_uint64 == 64);  // compatibility with other devices
    bitvec.reserve((n + bits_uint64 - 1) / bits_uint64);
  }

  /**
   * Clears the bitset.
   */
  void clear() {
    num_bits = 0;
    bitvec.clear();
  }

  /**
   * Shrinks the allocation for bitset to its current size.
   */
  void shrink_to_fit() { bitvec.shrink_to_fit(); }

  /**
   * Gets the size of the bitset
   * @returns The number of bits held by the bitset
   */
  size_t size() const { return num_bits; }

  /**
   * Gets the space taken by the bitset
   * @returns the space in bytes taken by this bitset
   */
  // size_t alloc_size() const { return bitvec.size() * sizeof(uint64_t); }

  /**
   * Unset every bit in the bitset.
   */
  void reset() { std::fill(bitvec.begin(), bitvec.end(), 0); }

  /**
   * Unset a range of bits given an inclusive range
   *
   * @param begin first bit in range to reset
   * @param end last bit in range to reset
   */
  void reset(size_t begin, size_t end) {
    if (num_bits == 0) {
      return;
    }

    KATANA_LOG_DEBUG_ASSERT(begin <= (num_bits - 1));
    KATANA_LOG_DEBUG_ASSERT(end <= (num_bits - 1));

    // 100% safe implementation, but slow
    // for (unsigned long i = begin; i <= end; i++) {
    //  size_t bit_index = i / bits_uint64;
    //  uint64_t bit_offset = 1;
    //  bit_offset <<= (i % bits_uint64);
    //  uint64_t mask = ~bit_offset;
    //  bitvec[bit_index] &= mask;
    //}

    // block which you are safe to clear
    size_t vec_begin = (begin + bits_uint64 - 1) / bits_uint64;
    size_t vec_end;

    if (end == (num_bits - 1))
      vec_end = bitvec.size();
    else
      vec_end = (end + 1) / bits_uint64;  // floor

    if (vec_begin < vec_end) {
      std::fill(bitvec.begin() + vec_begin, bitvec.begin() + vec_end, 0);
    }

    vec_begin *= bits_uint64;
    vec_end *= bits_uint64;

    // at this point vec_begin -> vec_end-1 has been reset

    if (vec_begin > vec_end) {
      // no fill happened
      if (begin < vec_begin) {
        size_t diff = vec_begin - begin;
        KATANA_LOG_DEBUG_ASSERT(diff < 64);
        uint64_t mask = (uint64_t{1} << (64 - diff)) - 1;

        size_t end_diff = end - vec_end + 1;
        uint64_t or_mask = (uint64_t{1} << end_diff) - 1;
        mask |= ~or_mask;

        size_t bit_index = begin / bits_uint64;
        bitvec[bit_index] &= mask;
      }
    } else {
      if (begin < vec_begin) {
        size_t diff = vec_begin - begin;
        KATANA_LOG_DEBUG_ASSERT(diff < 64);
        uint64_t mask = (uint64_t{1} << (64 - diff)) - 1;
        size_t bit_index = begin / bits_uint64;
        bitvec[bit_index] &= mask;
      }
      if (end >= vec_end) {
        size_t diff = end - vec_end + 1;
        KATANA_LOG_DEBUG_ASSERT(diff < 64);
        uint64_t mask = (uint64_t{1} << diff) - 1;
        size_t bit_index = end / bits_uint64;
        bitvec[bit_index] &= ~mask;
      }
    }
  }

  /**
   * Check a bit to see if it is currently set.
   * Using this is recommended only if set() and reset()
   * are not being used in that parallel section/phase
   *
   * @param index Bit to check to see if set
   * @returns true if index is set
   */
  bool test(size_t index) const {
    KATANA_LOG_DEBUG_ASSERT(index < num_bits);
    size_t bit_index = index / bits_uint64;
    uint64_t bit_offset = 1;
    bit_offset <<= (index % bits_uint64);
    return (
        (bitvec[bit_index].load(std::memory_order_relaxed) & bit_offset) != 0);
  }

  /**
   * Set a bit in the bitset.
   *
   * @param index Bit to set
   * @returns the old value
   */
  bool set(size_t index) {
    size_t bit_index = index / bits_uint64;
    uint64_t bit_offset = 1;
    bit_offset <<= (index % bits_uint64);
    uint64_t old_val = bitvec[bit_index];
    // test and set
    // if old_bit is 0, then atomically set it
    while (((old_val & bit_offset) == 0) &&
           !bitvec[bit_index].compare_exchange_weak(
               old_val, old_val | bit_offset, std::memory_order_relaxed)) {
      ;
    }
    return (old_val & bit_offset);
  }

  /**
   * Reset a bit in the bitset.
   *
   * @param index Bit to reset
   * @returns the old value
   */
  bool reset(size_t index) {
    size_t bit_index = index / bits_uint64;
    uint64_t bit_offset = 1;
    bit_offset <<= (index % bits_uint64);
    uint64_t old_val = bitvec[bit_index];
    // test and reset
    // if old_bit is 1, then atomically reset it
    while (((old_val & bit_offset) != 0) &&
           !bitvec[bit_index].compare_exchange_weak(
               old_val, old_val & ~bit_offset, std::memory_order_relaxed)) {
      ;
    }
    return (old_val & bit_offset);
  }

  // assumes bit_vector is not updated (set) in parallel
  void bitwise_or(const DynamicBitset& other);

  // assumes bit_vector is not updated (set) in parallel
  void bitwise_not();

  // assumes bit_vector is not updated (set) in parallel

  /**
   * Does an IN-PLACE bitwise and of this bitset and another bitset
   *
   * @param other Other bitset to do bitwise and with
   */
  void bitwise_and(const DynamicBitset& other);

  /**
   * Does an IN-PLACE bitwise and of 2 passed in bitsets and saves to this
   * bitset
   *
   * @param other1 Bitset to and with other 2
   * @param other2 Bitset to and with other 1
   */
  void bitwise_and(const DynamicBitset& other1, const DynamicBitset& other2);

  /**
   * Does an IN-PLACE bitwise xor of this bitset and another bitset
   *
   * @param other Other bitset to do bitwise xor with
   */
  void bitwise_xor(const DynamicBitset& other);

  /**
   * Does an IN-PLACE bitwise and of 2 passed in bitsets and saves to this
   * bitset
   *
   * @param other1 Bitset to xor with other 2
   * @param other2 Bitset to xor with other 1
   */
  void bitwise_xor(const DynamicBitset& other1, const DynamicBitset& other2);

  /**
   * Count how many bits are set in the bitset
   *
   * @returns number of set bits in the bitset
   */
  uint64_t count() const;

  /**
   * Returns a vector containing the set bits in this bitset in order
   * from left to right.
   * Do NOT call in a parallel region as it uses katana::on_each.
   *
   * @returns vector with offsets into set bits
   */
  template <typename integer>
  std::vector<integer> GetOffsets() const;

  /**
   * Given a vector, appends the set bits in this bitset in order
   * from left to right into the vector.
   * Do NOT call in a parallel region as it uses katana::on_each.
   */
  template <typename integer>
  void AppendOffsets(std::vector<integer>* vec) const;

  //! this is defined to
  using tt_is_copyable = int;
};

template <>
std::vector<uint32_t> DynamicBitset::GetOffsets() const;

template <>
std::vector<uint64_t> DynamicBitset::GetOffsets() const;

template <>
void DynamicBitset::AppendOffsets(std::vector<uint32_t>* offsets) const;

template <>
void DynamicBitset::AppendOffsets(std::vector<uint64_t>* offsets) const;

//! An empty bitset object; used mainly by InvalidBitsetFn
extern katana::DynamicBitset EmptyBitset;

//! A structure representing an empty bitset.
struct KATANA_EXPORT InvalidBitsetFn {
  //! Returns false as this is an empty bitset (invalid)
  static constexpr bool is_valid() { return false; }

  //! Returns the empty bitset
  static katana::DynamicBitset& get() { return EmptyBitset; }

  //! No-op since it's an empty bitset
  static void reset_range(size_t, size_t) {}
};
}  // namespace katana
#endif
