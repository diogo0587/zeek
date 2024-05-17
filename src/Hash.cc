// See the file "COPYING" in the main distribution directory for copyright.

#include "zeek/Hash.h"

#include "zeek/zeek-config.h"

#include <highwayhash/highwayhash_target.h>
#include <highwayhash/instruction_sets.h>
#include <highwayhash/sip_hash.h>

#include "zeek/3rdparty/doctest.h"
#include "zeek/DebugLogger.h"
#include "zeek/Desc.h"
#include "zeek/Reporter.h"
#include "zeek/Val.h" // needed for const.bif
#include "zeek/ZeekString.h"
#include "zeek/digest.h"

#include "const.bif.netvar_h"

namespace zeek::detail {

alignas(32) uint64_t KeyedHash::shared_highwayhash_key[4];
alignas(32) uint64_t KeyedHash::cluster_highwayhash_key[4];
alignas(16) unsigned long long KeyedHash::shared_siphash_key[2];

// we use the following lines to not pull in the highwayhash headers in Hash.h - but to check the
// types did not change underneath us.
static_assert(std::is_same_v<hash64_t, highwayhash::HHResult64>, "Highwayhash return values must match hash_x_t");
static_assert(std::is_same_v<hash128_t, highwayhash::HHResult128>, "Highwayhash return values must match hash_x_t");
static_assert(std::is_same_v<hash256_t, highwayhash::HHResult256>, "Highwayhash return values must match hash_x_t");

void KeyedHash::InitializeSeeds(const std::array<uint32_t, SEED_INIT_SIZE>& seed_data) {
    static_assert(std::is_same_v<decltype(KeyedHash::shared_siphash_key), highwayhash::SipHashState::Key>,
                  "Highwayhash Key is not unsigned long long[2]");
    static_assert(std::is_same_v<decltype(KeyedHash::shared_highwayhash_key), highwayhash::HHKey>,
                  "Highwayhash HHKey is not uint64_t[4]");
    if ( seeds_initialized )
        return;

    internal_sha1(reinterpret_cast<const u_char*>(seed_data.data()), sizeof(seed_data) - 16,
                  shared_hmac_md5_key); // The last 128 bits of buf are for siphash

    static_assert(sizeof(shared_highwayhash_key) == ZEEK_SHA256_DIGEST_LENGTH);
    calculate_digest(Hash_SHA256, (const u_char*)seed_data.data(), sizeof(seed_data) - 16,
                     reinterpret_cast<unsigned char*>(shared_highwayhash_key));

    memcpy(shared_siphash_key, reinterpret_cast<const char*>(seed_data.data()) + 64, 16);

    seeds_initialized = true;
}

void KeyedHash::InitOptions() {
    calculate_digest(Hash_SHA256, BifConst::digest_salt->Bytes(), BifConst::digest_salt->Len(),
                     reinterpret_cast<unsigned char*>(cluster_highwayhash_key));
}

hash64_t KeyedHash::Hash64(const void* bytes, uint64_t size) {
    return highwayhash::SipHash(shared_siphash_key, static_cast<const char*>(bytes), size);
}

void KeyedHash::Hash128(const void* bytes, uint64_t size, hash128_t* result) {
    highwayhash::InstructionSets::Run<highwayhash::HighwayHash>(shared_highwayhash_key, static_cast<const char*>(bytes),
                                                                size, result);
}

void KeyedHash::Hash256(const void* bytes, uint64_t size, hash256_t* result) {
    highwayhash::InstructionSets::Run<highwayhash::HighwayHash>(shared_highwayhash_key, static_cast<const char*>(bytes),
                                                                size, result);
}

hash64_t KeyedHash::StaticHash64(const void* bytes, uint64_t size) {
    hash64_t result = 0;
    highwayhash::InstructionSets::Run<highwayhash::HighwayHash>(cluster_highwayhash_key,
                                                                static_cast<const char*>(bytes), size, &result);
    return result;
}

void KeyedHash::StaticHash128(const void* bytes, uint64_t size, hash128_t* result) {
    highwayhash::InstructionSets::Run<highwayhash::HighwayHash>(cluster_highwayhash_key,
                                                                static_cast<const char*>(bytes), size, result);
}

void KeyedHash::StaticHash256(const void* bytes, uint64_t size, hash256_t* result) {
    highwayhash::InstructionSets::Run<highwayhash::HighwayHash>(cluster_highwayhash_key,
                                                                static_cast<const char*>(bytes), size, result);
}

void init_hash_function() {
    // Make sure we have already called init_random_seed().
    if ( ! KeyedHash::IsInitialized() )
        reporter->InternalError("Zeek's hash functions aren't fully initialized");
}

HashKey::HashKey(bool b) { Set(b); }

HashKey::HashKey(int i) { Set(i); }

HashKey::HashKey(zeek_int_t bi) { Set(bi); }

HashKey::HashKey(zeek_uint_t bu) { Set(bu); }

HashKey::HashKey(uint32_t u) { Set(u); }

HashKey::HashKey(const uint32_t u[], size_t n) {
    size = write_size = n * sizeof(u[0]);
    key = (char*)u;
}

HashKey::HashKey(double d) { Set(d); }

HashKey::HashKey(const void* p) { Set(p); }

HashKey::HashKey(const char* s) {
    size = write_size = strlen(s); // note - skip final \0
    key = (char*)s;
}

HashKey::HashKey(const String* s) {
    size = write_size = s->Len();
    key = (char*)s->Bytes();
}

HashKey::HashKey(const void* bytes, size_t arg_size) {
    size = write_size = arg_size;
    key = CopyKey((char*)bytes, size);
    is_our_dynamic = true;
}

HashKey::HashKey(const void* arg_key, size_t arg_size, hash_t arg_hash) {
    size = write_size = arg_size;
    hash = arg_hash;
    key = CopyKey((char*)arg_key, size);
    is_our_dynamic = true;
}

HashKey::HashKey(const void* arg_key, size_t arg_size, hash_t arg_hash, bool /* dont_copy */) {
    size = write_size = arg_size;
    hash = arg_hash;
    key = (char*)arg_key;
}

HashKey::HashKey(const HashKey& other) : HashKey(other.key, other.size, other.hash) {}

HashKey::HashKey(HashKey&& other) noexcept {
    hash = other.hash;
    size = other.size;
    write_size = other.write_size;
    read_size = other.read_size;

    is_our_dynamic = other.is_our_dynamic;
    key = other.key;

    other.size = 0;
    other.is_our_dynamic = false;
    other.key = nullptr;
}

HashKey::~HashKey() {
    if ( is_our_dynamic )
        delete[] reinterpret_cast<char*>(key);
}

hash_t HashKey::Hash() const {
    if ( hash == 0 )
        hash = HashBytes(key, size);
#ifdef DEBUG
    if ( zeek::detail::debug_logger.IsEnabled(DBG_HASHKEY) ) {
        ODesc d;
        Describe(&d);
        DBG_LOG(DBG_HASHKEY, "HashKey %p %s", this, d.Description());
    }
#endif
    return hash;
}

void* HashKey::TakeKey() {
    if ( is_our_dynamic ) {
        is_our_dynamic = false;
        return key;
    }
    else
        return CopyKey(key, size);
}

void HashKey::Describe(ODesc* d) const {
    char buf[64];
    snprintf(buf, 16, "%0" PRIx64, hash);
    d->Add(buf);
    d->SP();

    if ( size > 0 ) {
        d->Add(IsAllocated() ? "(" : "[");

        for ( size_t i = 0; i < size; i++ ) {
            if ( i > 0 ) {
                d->SP();
                // Extra spacing every 8 bytes, for readability.
                if ( i % 8 == 0 )
                    d->SP();
            }

            // Don't display unwritten content, only say how much there is.
            if ( i > write_size ) {
                d->Add("<+");
                d->Add(static_cast<uint64_t>(size - write_size - 1));
                d->Add(" of ");
                d->Add(static_cast<uint64_t>(size));
                d->Add(" available>");
                break;
            }

            snprintf(buf, 3, "%02x", key[i]);
            d->Add(buf);
        }

        d->Add(IsAllocated() ? ")" : "]");
    }
}

char* HashKey::CopyKey(const char* k, size_t s) const {
    char* k_copy = new char[s]; // s == 0 is okay, returns non-nil
    memcpy(k_copy, k, s);
    return k_copy;
}

hash_t HashKey::HashBytes(const void* bytes, size_t size) { return KeyedHash::Hash64(bytes, size); }

void HashKey::Set(bool b) {
    key_u.b = b;
    key = reinterpret_cast<char*>(&key_u);
    size = write_size = sizeof(b);
}

void HashKey::Set(int i) {
    key_u.i = i;
    key = reinterpret_cast<char*>(&key_u);
    size = write_size = sizeof(i);
}

void HashKey::Set(zeek_int_t bi) {
    key_u.bi = bi;
    key = reinterpret_cast<char*>(&key_u);
    size = write_size = sizeof(bi);
}

void HashKey::Set(zeek_uint_t bu) {
    key_u.bi = zeek_int_t(bu);
    key = reinterpret_cast<char*>(&key_u);
    size = write_size = sizeof(bu);
}

void HashKey::Set(uint32_t u) {
    key_u.u32 = u;
    key = reinterpret_cast<char*>(&key_u);
    size = write_size = sizeof(u);
}

void HashKey::Set(double d) {
    key_u.d = d;
    key = reinterpret_cast<char*>(&key_u);
    size = write_size = sizeof(d);
}

void HashKey::Set(const void* p) {
    key_u.p = p;
    key = reinterpret_cast<char*>(&key_u);
    size = write_size = sizeof(p);
}

void HashKey::Reserve(const char* tag, size_t addl_size, size_t alignment) {
    ASSERT(! IsAllocated());
    size_t s0 = size;
    size_t s1 = util::memory_size_align(size, alignment);
    size = s1 + addl_size;

    DBG_LOG(DBG_HASHKEY, "HashKey %p reserving %lu/%lu: %lu -> %lu -> %lu [%s]", this, addl_size, alignment, s0, s1,
            size, tag);
}

void HashKey::Allocate() {
    if ( key != nullptr && key != reinterpret_cast<char*>(&key_u) ) {
        reporter->InternalWarning("usage error in HashKey::Allocate(): already allocated");
        return;
    }

    is_our_dynamic = true;
    key = reinterpret_cast<char*>(new double[size / sizeof(double) + 1]);

    read_size = 0;
    write_size = 0;
}

void HashKey::Write(const char* tag, bool b) { Write(tag, &b, sizeof(b), 0); }

void HashKey::Write(const char* tag, int i, bool align) {
    if ( ! IsAllocated() ) {
        Set(i);
        return;
    }

    Write(tag, &i, sizeof(i), align ? sizeof(i) : 0);
}

void HashKey::Write(const char* tag, zeek_int_t bi, bool align) {
    if ( ! IsAllocated() ) {
        Set(bi);
        return;
    }

    Write(tag, &bi, sizeof(bi), align ? sizeof(bi) : 0);
}

void HashKey::Write(const char* tag, zeek_uint_t bu, bool align) {
    if ( ! IsAllocated() ) {
        Set(bu);
        return;
    }

    Write(tag, &bu, sizeof(bu), align ? sizeof(bu) : 0);
}

void HashKey::Write(const char* tag, uint32_t u, bool align) {
    if ( ! IsAllocated() ) {
        Set(u);
        return;
    }

    Write(tag, &u, sizeof(u), align ? sizeof(u) : 0);
}

void HashKey::Write(const char* tag, double d, bool align) {
    if ( ! IsAllocated() ) {
        Set(d);
        return;
    }

    Write(tag, &d, sizeof(d), align ? sizeof(d) : 0);
}

void HashKey::Write(const char* tag, const void* bytes, size_t n, size_t alignment) {
    size_t s0 = write_size;
    AlignWrite(alignment);
    size_t s1 = write_size;
    EnsureWriteSpace(n);

    memcpy(key + write_size, bytes, n);
    write_size += n;

    DBG_LOG(DBG_HASHKEY, "HashKey %p writing %lu/%lu: %lu -> %lu -> %lu [%s]", this, n, alignment, s0, s1, write_size,
            tag);
}

void HashKey::SkipWrite(const char* tag, size_t n) {
    DBG_LOG(DBG_HASHKEY, "HashKey %p skip-writing %lu: %lu -> %lu [%s]", this, n, write_size, write_size + n, tag);

    EnsureWriteSpace(n);
    write_size += n;
}

void HashKey::AlignWrite(size_t alignment) {
    ASSERT(IsAllocated());

    if ( alignment == 0 )
        return;

    size_t old_size = write_size;

    write_size = util::memory_size_align(write_size, alignment);

    if ( write_size > size )
        reporter->InternalError(
            "buffer overflow in HashKey::AlignWrite(): "
            "after alignment, %lu bytes used of %lu allocated",
            write_size, size);

    while ( old_size < write_size )
        key[old_size++] = '\0';
}

void HashKey::AlignRead(size_t alignment) const {
    ASSERT(IsAllocated());

    if ( alignment == 0 )
        return;

    int old_size = read_size;

    read_size = util::memory_size_align(read_size, alignment);

    if ( read_size > size )
        reporter->InternalError(
            "buffer overflow in HashKey::AlignRead(): "
            "after alignment, %lu bytes used of %lu allocated",
            read_size, size);
}

void HashKey::Read(const char* tag, bool& b) const { Read(tag, &b, sizeof(b), 0); }

void HashKey::Read(const char* tag, int& i, bool align) const { Read(tag, &i, sizeof(i), align ? sizeof(i) : 0); }

void HashKey::Read(const char* tag, zeek_int_t& i, bool align) const {
    Read(tag, &i, sizeof(i), align ? sizeof(i) : 0);
}

void HashKey::Read(const char* tag, zeek_uint_t& u, bool align) const {
    Read(tag, &u, sizeof(u), align ? sizeof(u) : 0);
}

void HashKey::Read(const char* tag, uint32_t& u, bool align) const { Read(tag, &u, sizeof(u), align ? sizeof(u) : 0); }

void HashKey::Read(const char* tag, double& d, bool align) const { Read(tag, &d, sizeof(d), align ? sizeof(d) : 0); }

void HashKey::Read(const char* tag, void* out, size_t n, size_t alignment) const {
    size_t s0 = read_size;
    AlignRead(alignment);
    size_t s1 = read_size;
    EnsureReadSpace(n);

    // In case out is nil, make sure nothing is to be read, and only memcpy
    // when there is a non-zero amount. Memory checkers don't nullpointers
    // in memcpy even if the size is 0.
    ASSERT(out != nullptr || (out == nullptr && n == 0));

    if ( n > 0 ) {
        memcpy(out, key + read_size, n);
        read_size += n;
    }

    DBG_LOG(DBG_HASHKEY, "HashKey %p reading %lu/%lu: %lu -> %lu -> %lu [%s]", this, n, alignment, s0, s1, read_size,
            tag);
}

void HashKey::SkipRead(const char* tag, size_t n) const {
    DBG_LOG(DBG_HASHKEY, "HashKey %p skip-reading %lu: %lu -> %lu [%s]", this, n, read_size, read_size + n, tag);

    EnsureReadSpace(n);
    read_size += n;
}

void HashKey::EnsureWriteSpace(size_t n) const {
    if ( n == 0 )
        return;

    if ( ! IsAllocated() )
        reporter->InternalError(
            "usage error in HashKey::EnsureWriteSpace(): "
            "size-checking unreserved buffer");
    if ( write_size + n > size )
        reporter->InternalError(
            "buffer overflow in HashKey::Write(): writing %lu "
            "bytes with %lu remaining",
            n, size - write_size);
}

void HashKey::EnsureReadSpace(size_t n) const {
    if ( n == 0 )
        return;

    if ( ! IsAllocated() )
        reporter->InternalError(
            "usage error in HashKey::EnsureReadSpace(): "
            "size-checking unreserved buffer");
    if ( read_size + n > size )
        reporter->InternalError(
            "buffer overflow in HashKey::EnsureReadSpace(): reading %lu "
            "bytes with %lu remaining",
            n, size - read_size);
}

bool HashKey::operator==(const HashKey& other) const {
    // Quick exit for the same object.
    if ( this == &other )
        return true;

    return Equal(other.key, other.size, other.hash);
}

bool HashKey::operator!=(const HashKey& other) const {
    // Quick exit for different objects.
    if ( this != &other )
        return true;

    return ! Equal(other.key, other.size, other.hash);
}

bool HashKey::Equal(const void* other_key, size_t other_size, hash_t other_hash) const {
    // If the key memory is the same just return true.
    if ( key == other_key && size == other_size )
        return true;

    // If either key is nullptr, return false. If they were both nullptr, it
    // would have fallen into the above block already.
    if ( key == nullptr || other_key == nullptr )
        return false;

    return (hash == other_hash) && (size == other_size) && (memcmp(key, other_key, size) == 0);
}

HashKey& HashKey::operator=(const HashKey& other) {
    if ( this == &other )
        return *this;

    if ( is_our_dynamic && IsAllocated() )
        delete[] key;

    hash = other.hash;
    size = other.size;
    is_our_dynamic = true;
    write_size = other.write_size;
    read_size = other.read_size;

    key = CopyKey(other.key, other.size);

    return *this;
}

HashKey& HashKey::operator=(HashKey&& other) noexcept {
    if ( this == &other )
        return *this;

    hash = other.hash;
    size = other.size;
    write_size = other.write_size;
    read_size = other.read_size;

    if ( is_our_dynamic && IsAllocated() )
        delete[] key;

    is_our_dynamic = other.is_our_dynamic;
    key = other.key;

    other.size = 0;
    other.is_our_dynamic = false;
    other.key = nullptr;

    return *this;
}

TEST_SUITE_BEGIN("Hash");

TEST_CASE("equality") {
    HashKey h1(12345);
    HashKey h2(12345);
    HashKey h3(67890);

    CHECK(h1 == h2);
    CHECK(h1 != h3);
}

TEST_CASE("copy assignment") {
    HashKey h1(12345);
    HashKey h2 = h1;
    HashKey h3{h1};

    CHECK(h1 == h2);
    CHECK(h1 == h3);
}

TEST_CASE("move assignment") {
    HashKey h1(12345);
    HashKey h2(12345);
    HashKey h3(12345);

    HashKey h4 = std::move(h2);
    HashKey h5{h3};

    CHECK(h1 == h4);
    CHECK(h1 == h5);
}

TEST_SUITE_END();

} // namespace zeek::detail
