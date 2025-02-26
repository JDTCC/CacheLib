#include <gtest/gtest.h>

#include "cachelib/allocator/CacheAllocator.h"
#include "cachelib/experimental/objcache2/ObjectCache.h"
#include "cachelib/experimental/objcache2/persistence/gen-cpp2/persistent_data_types.h"
#include "cachelib/experimental/objcache2/tests/gen-cpp2/test_object_types.h"

namespace facebook {
namespace cachelib {
namespace objcache2 {
namespace test {
namespace {
struct Foo {
  int a{};
  int b{};
  int c{};
};

struct Foo2 {
  int d{};
  int e{};
  int f{};
};

struct Foo3 {
  explicit Foo3(int& n) : numDtors_{n} {}
  ~Foo3() { numDtors_++; }
  int& numDtors_;
};

struct FooBase {
  virtual ~FooBase() {}
};

struct Foo4 : FooBase {
  int a{};
  int b{};
  int c{};
};

struct Foo5 : FooBase {
  int d{};
  int e{};
  int f{};
};
} // namespace

template <typename AllocatorT>
class ObjectCacheTest : public ::testing::Test {
 public:
  using ObjectCache = ObjectCache<AllocatorT>;
  using ObjectCacheConfig = typename ObjectCache::Config;
  void testGetAllocSize() {
    std::vector<uint8_t> maxKeySizes{};
    std::vector<uint32_t> allocSizes{};

    for (uint8_t keySize = 8; keySize < 255; keySize++) {
      maxKeySizes.push_back(keySize);
      allocSizes.push_back(ObjectCache::getL1AllocSize(keySize));
    }

    for (size_t i = 0; i < maxKeySizes.size(); i++) {
      EXPECT_TRUE(allocSizes[i] >= ObjectCache::kL1AllocSizeMin);
      EXPECT_TRUE(maxKeySizes[i] + sizeof(ObjectCacheItem) +
                      sizeof(typename AllocatorT::Item) <=
                  allocSizes[i]);
      EXPECT_TRUE(allocSizes[i] % 8 == 0);
    }
  }

  void testConfigValidation() {
    {
      ObjectCacheConfig config;
      config.setCacheName("test").setCacheCapacity(10'000);
      EXPECT_THROW(config.validate(), std::invalid_argument);

      EXPECT_THROW(ObjectCache::create(config), std::invalid_argument);

      config.setItemDestructor(
          [&](ObjectCacheDestructorData data) { data.deleteObject<Foo>(); });
      EXPECT_NO_THROW(ObjectCache::create(config));

      config.setCacheName("");
      EXPECT_THROW(ObjectCache::create(config), std::invalid_argument);
    }

    {
      // test size-aware cache config
      ObjectCacheConfig config;
      config.setCacheName("test").setItemDestructor(
          [&](ObjectCacheDestructorData data) { data.deleteObject<Foo>(); });

      // missing sizeControllerIntervalMs
      EXPECT_THROW(config.setCacheCapacity(10'000, 100'000),
                   std::invalid_argument);

      // missing cacheSizeLimit
      EXPECT_THROW(config.setCacheCapacity(10'000, 0, 10),
                   std::invalid_argument);

      config.setCacheCapacity(10'000, 100'000, 10);
      EXPECT_NO_THROW(config.validate());
    }

    {
      ObjectCacheConfig config;
      // invalid thread count
      EXPECT_THROW(config.enablePersistence(
                       0, "persistent_file",
                       [&](typename ObjectCache::Serializer serializer) {
                         return serializer.template serialize<ThriftFoo>();
                       },
                       [&](typename ObjectCache::Deserializer deserializer) {
                         return deserializer.template deserialize<ThriftFoo>();
                       }),
                   std::invalid_argument);
      // invalid file path
      EXPECT_THROW(config.enablePersistence(
                       1, "",
                       [&](typename ObjectCache::Serializer serializer) {
                         return serializer.template serialize<ThriftFoo>();
                       },
                       [&](typename ObjectCache::Deserializer deserializer) {
                         return deserializer.template deserialize<ThriftFoo>();
                       }),
                   std::invalid_argument);
      // missing serialize callback
      EXPECT_THROW(config.enablePersistence(
                       1, "persistent_file", nullptr,
                       [&](typename ObjectCache::Deserializer deserializer) {
                         return deserializer.template deserialize<ThriftFoo>();
                       }),
                   std::invalid_argument);
      // missing deserialize callback
      EXPECT_THROW(config.enablePersistence(
                       1, "persistent_file",
                       [&](typename ObjectCache::Serializer serializer) {
                         return serializer.template serialize<ThriftFoo>();
                       },
                       nullptr),
                   std::invalid_argument);
    }
  }

  void testSimple() {
    ObjectCacheConfig config;
    config.setCacheName("test").setCacheCapacity(10'000);
    config.setItemDestructor(
        [&](ObjectCacheDestructorData data) { data.deleteObject<Foo>(); });
    auto objcache = ObjectCache::create(config);

    auto found1 = objcache->template find<Foo>("Foo");
    EXPECT_EQ(nullptr, found1);

    auto foo = std::make_unique<Foo>();
    foo->a = 1;
    foo->b = 2;
    foo->c = 3;
    auto res = objcache->insertOrReplace("Foo", std::move(foo));
    EXPECT_EQ(ObjectCache::AllocStatus::kSuccess, res.first);
    ASSERT_NE(nullptr, res.second);
    EXPECT_EQ(1, res.second->a);
    EXPECT_EQ(2, res.second->b);
    EXPECT_EQ(3, res.second->c);

    auto found2 = objcache->template find<Foo>("Foo");
    ASSERT_NE(nullptr, found2);
    EXPECT_EQ(1, found2->a);
    EXPECT_EQ(2, found2->b);
    EXPECT_EQ(3, found2->c);
  }

  void testMultiType() {
    ObjectCacheConfig config;
    config.setCacheName("test").setCacheCapacity(10'000).setItemDestructor(
        [&](ObjectCacheDestructorData data) {
          if (data.key == "Foo") {
            data.deleteObject<Foo>();
          } else if (data.key == "Foo2") {
            data.deleteObject<Foo2>();
          }
        });

    auto objcache = ObjectCache::create(config);

    auto foo = std::make_unique<Foo>();
    foo->a = 1;
    foo->b = 2;
    foo->c = 3;
    auto res1 = objcache->insertOrReplace("Foo", std::move(foo));
    EXPECT_EQ(ObjectCache::AllocStatus::kSuccess, res1.first);

    auto found1 = objcache->template find<Foo>("Foo");
    ASSERT_NE(nullptr, found1);
    EXPECT_EQ(1, found1->a);
    EXPECT_EQ(2, found1->b);
    EXPECT_EQ(3, found1->c);

    auto foo2 = std::make_unique<Foo2>();
    foo2->d = 4;
    foo2->e = 5;
    foo2->f = 6;
    auto res2 = objcache->insertOrReplace("Foo2", std::move(foo2));
    EXPECT_EQ(ObjectCache::AllocStatus::kSuccess, res2.first);

    auto found2 = objcache->template find<Foo2>("Foo2");
    ASSERT_NE(nullptr, found2);
    EXPECT_EQ(4, found2->d);
    EXPECT_EQ(5, found2->e);
    EXPECT_EQ(6, found2->f);
  }

  void testMultiTypePolymorphism() {
    ObjectCacheConfig config;
    config.setCacheName("test").setCacheCapacity(10'000).setItemDestructor(
        [&](ObjectCacheDestructorData data) { data.deleteObject<FooBase>(); });

    auto objcache = ObjectCache::create(config);

    auto foo4 = std::make_unique<Foo4>();
    foo4->a = 1;
    foo4->b = 2;
    foo4->c = 3;
    auto res1 = objcache->insertOrReplace("Foo4", std::move(foo4));
    EXPECT_EQ(ObjectCache::AllocStatus::kSuccess, res1.first);

    auto found1 = objcache->template find<Foo4>("Foo4");
    ASSERT_NE(nullptr, found1);
    EXPECT_EQ(1, found1->a);
    EXPECT_EQ(2, found1->b);
    EXPECT_EQ(3, found1->c);

    auto foo5 = std::make_unique<Foo5>();
    foo5->d = 4;
    foo5->e = 5;
    foo5->f = 6;
    auto res2 = objcache->insertOrReplace("Foo5", std::move(foo5));
    EXPECT_EQ(ObjectCache::AllocStatus::kSuccess, res2.first);

    auto found2 = objcache->template find<Foo5>("Foo5");
    ASSERT_NE(nullptr, found2);
    EXPECT_EQ(4, found2->d);
    EXPECT_EQ(5, found2->e);
    EXPECT_EQ(6, found2->f);
  }

  void testUserItemDestructor() {
    int numDtors = 0;
    ObjectCacheConfig config;
    config.setCacheName("test").setCacheCapacity(10'000).setItemDestructor(
        [&](ObjectCacheDestructorData data) { data.deleteObject<Foo3>(); });
    auto objcache = ObjectCache::create(config);
    for (int i = 0; i < 10; i++) {
      objcache->insertOrReplace(folly::sformat("key_{}", i),
                                std::make_unique<Foo3>(numDtors));
    }
    for (int i = 0; i < 10; i++) {
      objcache->remove(folly::sformat("key_{}", i));
    }
    ASSERT_EQ(10, numDtors);
  }

  void testExpiration() {
    ObjectCacheConfig config;
    config.setCacheName("test").setCacheCapacity(10'000).setItemDestructor(
        [&](ObjectCacheDestructorData data) { data.deleteObject<Foo>(); });
    auto objcache = ObjectCache::create(config);

    auto foo = std::make_unique<Foo>();
    foo->a = 1;
    foo->b = 2;
    foo->c = 3;

    int ttlSecs = 2;
    // test bad API call
    ASSERT_THROW(objcache->insertOrReplace(
                     "Foo", std::move(std::make_unique<Foo>()), ttlSecs),
                 std::invalid_argument);

    objcache->insertOrReplace("Foo", std::move(foo), 0 /*object size*/,
                              ttlSecs);

    auto found1 = objcache->template find<Foo>("Foo");
    ASSERT_NE(nullptr, found1);
    EXPECT_EQ(1, found1->a);
    EXPECT_EQ(2, found1->b);
    EXPECT_EQ(3, found1->c);

    std::this_thread::sleep_for(std::chrono::seconds{3});
    auto found2 = objcache->template find<Foo>("Foo");
    ASSERT_EQ(nullptr, found2);
  }

  void testReplace() {
    ObjectCacheConfig config;
    config.setCacheName("test").setCacheCapacity(10'000).setItemDestructor(
        [&](ObjectCacheDestructorData data) { data.deleteObject<Foo>(); });
    auto objcache = ObjectCache::create(config);

    auto foo1 = std::make_unique<Foo>();
    foo1->a = 1;
    foo1->b = 2;
    foo1->c = 3;
    std::shared_ptr<Foo> replaced;
    auto res =
        objcache->insertOrReplace("Foo", std::move(foo1), 0, 0, &replaced);
    EXPECT_EQ(ObjectCache::AllocStatus::kSuccess, res.first);
    EXPECT_EQ(nullptr, replaced);

    auto found1 = objcache->template find<Foo>("Foo");
    ASSERT_NE(nullptr, found1);
    EXPECT_EQ(1, found1->a);
    EXPECT_EQ(2, found1->b);
    EXPECT_EQ(3, found1->c);

    auto foo2 = std::make_unique<Foo>();
    foo2->a = 10;
    foo2->b = 20;
    foo2->c = 30;
    res = objcache->insertOrReplace("Foo", std::move(foo2), 0, 0, &replaced);
    EXPECT_EQ(ObjectCache::AllocStatus::kSuccess, res.first);
    ASSERT_NE(nullptr, replaced);
    EXPECT_EQ(1, replaced->a);
    EXPECT_EQ(2, replaced->b);
    EXPECT_EQ(3, replaced->c);

    auto found2 = objcache->template find<Foo>("Foo");
    ASSERT_NE(nullptr, found2);
    EXPECT_EQ(10, found2->a);
    EXPECT_EQ(20, found2->b);
    EXPECT_EQ(30, found2->c);
  }

  void testUniqueInsert() {
    ObjectCacheConfig config;
    config.setCacheName("test").setCacheCapacity(10'000).setItemDestructor(
        [&](ObjectCacheDestructorData data) { data.deleteObject<Foo>(); });
    auto objcache = ObjectCache::create(config);

    // test bad API call
    ASSERT_THROW(objcache->insert("Foo", std::move(std::make_unique<Foo>()),
                                  2 /* TTL seconds */),
                 std::invalid_argument);

    auto foo1 = std::make_unique<Foo>();
    foo1->a = 1;
    foo1->b = 2;
    foo1->c = 3;
    auto res = objcache->insert("Foo", std::move(foo1));
    EXPECT_EQ(ObjectCache::AllocStatus::kSuccess, res.first);

    auto found1 = objcache->template find<Foo>("Foo");
    ASSERT_NE(nullptr, found1);
    EXPECT_EQ(1, found1->a);
    EXPECT_EQ(2, found1->b);
    EXPECT_EQ(3, found1->c);

    auto foo2 = std::make_unique<Foo>();
    foo2->a = 10;
    foo2->b = 20;
    foo2->c = 30;
    res = objcache->insert("Foo", std::move(foo2));
    EXPECT_EQ(ObjectCache::AllocStatus::kKeyAlreadyExists, res.first);

    auto found2 = objcache->template find<Foo>("Foo");
    ASSERT_NE(nullptr, found1);
    EXPECT_EQ(1, found2->a);
    EXPECT_EQ(2, found2->b);
    EXPECT_EQ(3, found2->c);

    objcache->remove("Foo");
  }

  void testObjectSizeTrackingBasics() {
    ObjectCacheConfig config;
    config.setCacheName("test")
        .setCacheCapacity(10'000 /* l1EntriesLimit*/,
                          10'000'000 /* cacheSizeLimit */,
                          100 /* sizeControllerIntervalMs */)
        .setItemDestructor(
            [&](ObjectCacheDestructorData data) { data.deleteObject<Foo>(); });
    auto objcache = ObjectCache::create(config);
    EXPECT_EQ(objcache->getTotalObjectSize(), 0);
    auto foo1 = std::make_unique<Foo>();
    foo1->a = 1;
    foo1->b = 2;
    foo1->c = 3;
    auto foo1Size = 64;
    auto foo2 = std::make_unique<Foo>();
    foo2->a = 10;
    foo2->b = 20;
    foo2->c = 30;
    auto foo2Size = 128;

    // will throw without the object size
    ASSERT_THROW(objcache->insert("Foo", std::make_unique<Foo>()),
                 std::invalid_argument);

    // insert foo1
    {
      auto res = objcache->insert("Foo", std::move(foo1), foo1Size);
      ASSERT_EQ(ObjectCache::AllocStatus::kSuccess, res.first);

      auto found = objcache->template find<Foo>("Foo");
      ASSERT_NE(nullptr, found);
      ASSERT_EQ(1, found->a);
      ASSERT_EQ(2, found->b);
      ASSERT_EQ(3, found->c);
    }
    ASSERT_EQ(objcache->getNumEntries(), 1);
    ASSERT_EQ(objcache->getTotalObjectSize(), foo1Size);

    // replace foo1 with foo2
    {
      auto res = objcache->insertOrReplace("Foo", std::move(foo2), foo2Size);
      ASSERT_EQ(ObjectCache::AllocStatus::kSuccess, res.first);

      auto found = objcache->template find<Foo>("Foo");
      ASSERT_NE(nullptr, found);
      ASSERT_EQ(10, found->a);
      ASSERT_EQ(20, found->b);
      ASSERT_EQ(30, found->c);
    }
    ASSERT_EQ(objcache->getNumEntries(), 1);
    ASSERT_EQ(objcache->getTotalObjectSize(), foo2Size);

    // remove foo2
    objcache->remove("Foo");
    ASSERT_EQ(nullptr, objcache->template find<Foo>("Foo"));
    ASSERT_EQ(objcache->getNumEntries(), 0);
    ASSERT_EQ(objcache->getTotalObjectSize(), 0);
  }

  void testObjectSizeTrackingUniqueInsert() {
    ObjectCacheConfig config;
    config.setCacheName("test")
        .setCacheCapacity(10'000 /* l1EntriesLimit*/,
                          10'000'000 /* cacheSizeLimit */,
                          100 /* sizeControllerIntervalMs */)
        .setItemDestructor(
            [&](ObjectCacheDestructorData data) { data.deleteObject<Foo>(); });
    auto objcache = ObjectCache::create(config);

    // will throw without the object size
    ASSERT_THROW(objcache->insert("Foo", std::make_unique<Foo>()),
                 std::invalid_argument);

    auto foo1 = std::make_unique<Foo>();
    foo1->a = 1;
    foo1->b = 2;
    foo1->c = 3;
    auto foo1Size = 64;
    auto res = objcache->insert("Foo", std::move(foo1), foo1Size);
    EXPECT_EQ(ObjectCache::AllocStatus::kSuccess, res.first);
    ASSERT_EQ(objcache->getNumEntries(), 1);
    ASSERT_EQ(objcache->getTotalObjectSize(), foo1Size);

    auto found1 = objcache->template find<Foo>("Foo");
    ASSERT_NE(nullptr, found1);
    EXPECT_EQ(1, found1->a);
    EXPECT_EQ(2, found1->b);
    EXPECT_EQ(3, found1->c);

    auto foo2 = std::make_unique<Foo>();
    foo2->a = 10;
    foo2->b = 20;
    foo2->c = 30;
    auto foo2Size = 128;
    res = objcache->insert("Foo", std::move(foo2), foo2Size);
    EXPECT_EQ(ObjectCache::AllocStatus::kKeyAlreadyExists, res.first);
    ASSERT_EQ(objcache->getNumEntries(), 1);
    ASSERT_EQ(objcache->getTotalObjectSize(), foo1Size);

    auto found2 = objcache->template find<Foo>("Foo");
    ASSERT_NE(nullptr, found2);
    EXPECT_EQ(1, found2->a);
    EXPECT_EQ(2, found2->b);
    EXPECT_EQ(3, found2->c);
  }

  void testPersistence() {
    auto persistBaseFilePath = std::tmpnam(nullptr);
    ThriftFoo foo1;
    foo1.a().value() = 1;
    foo1.b().value() = 2;
    foo1.c().value() = 3;

    ThriftFoo foo2;
    foo2.a().value() = 4;
    foo2.b().value() = 5;
    foo2.c().value() = 6;

    auto objectSize1 = 1000;
    auto objectSize2 = 500;
    auto ttlSecs = 10;

    size_t threadsCount = 10;

    ObjectCacheConfig config;

    config.setCacheName("test")
        .setCacheCapacity(10'000 /*l1EntriesLimit*/)
        .setItemDestructor([&](ObjectCacheDestructorData data) {
          data.deleteObject<ThriftFoo>();
        })
        .enablePersistence(
            threadsCount, persistBaseFilePath,
            [&](typename ObjectCache::Serializer serializer) {
              return serializer.template serialize<ThriftFoo>();
            },
            [&](typename ObjectCache::Deserializer deserializer) {
              return deserializer.template deserialize<ThriftFoo>();
            });
    config.objectSizeTrackingEnabled = true;

    {
      auto objcache = ObjectCache::create(config);

      auto object1 = std::make_unique<ThriftFoo>(foo1);
      auto object2 = std::make_unique<ThriftFoo>(foo2);
      objcache->insertOrReplace("Foo1", std::move(object1), objectSize1,
                                ttlSecs);
      objcache->insertOrReplace("Foo2", std::move(object2), objectSize2);
      ASSERT_EQ(objcache->persist(), true);
    }

    // No objects should expire
    {
      auto objcache = ObjectCache::create(config);
      ASSERT_EQ(objcache->recover(), true);

      auto found1 = objcache->template find<ThriftFoo>("Foo1");
      ASSERT_NE(nullptr, found1);
      EXPECT_EQ(1, found1->a_ref());
      EXPECT_EQ(2, found1->b_ref());
      EXPECT_EQ(3, found1->c_ref());
      auto found2 = objcache->template find<ThriftFoo>("Foo2");
      ASSERT_NE(nullptr, found2);
      EXPECT_EQ(4, found2->a_ref());
      EXPECT_EQ(5, found2->b_ref());
      EXPECT_EQ(6, found2->c_ref());

      EXPECT_EQ(objectSize1 + objectSize2, objcache->getTotalObjectSize());
    }

    // Let Foo1 expire
    std::this_thread::sleep_for(std::chrono::seconds{15});
    {
      auto objcache = ObjectCache::create(config);
      ASSERT_EQ(objcache->recover(), true);

      auto found1 = objcache->template find<ThriftFoo>("Foo1");
      ASSERT_EQ(nullptr, found1);

      auto found2 = objcache->template find<ThriftFoo>("Foo2");
      ASSERT_NE(nullptr, found2);
      EXPECT_EQ(4, found2->a_ref());
      EXPECT_EQ(5, found2->b_ref());
      EXPECT_EQ(6, found2->c_ref());
      EXPECT_EQ(objectSize2, objcache->getTotalObjectSize());
    }

    // test recover failure
    {
      config.enablePersistence(
          threadsCount, "random_path",
          [&](typename ObjectCache::Serializer serializer) {
            return serializer.template serialize<ThriftFoo>();
          },
          [&](typename ObjectCache::Deserializer deserializer) {
            return deserializer.template deserialize<ThriftFoo>();
          });
      auto objcache = ObjectCache::create(config);
      ASSERT_EQ(objcache->recover(), false);
    }

    // test different thread count won't fail recover
    {
      config.enablePersistence(
          threadsCount - 2, persistBaseFilePath,
          [&](typename ObjectCache::Serializer serializer) {
            return serializer.template serialize<ThriftFoo>();
          },
          [&](typename ObjectCache::Deserializer deserializer) {
            return deserializer.template deserialize<ThriftFoo>();
          });

      auto objcache = ObjectCache::create(config);
      ASSERT_EQ(objcache->recover(), true);
      auto found = objcache->template find<ThriftFoo>("Foo2");
      ASSERT_NE(nullptr, found);
      EXPECT_EQ(4, found->a_ref());
      EXPECT_EQ(5, found->b_ref());
      EXPECT_EQ(6, found->c_ref());
      EXPECT_EQ(objectSize2, objcache->getTotalObjectSize());
    }
  }

  void testPersistenceMultiType() {
    auto persistBaseFilePath = std::tmpnam(nullptr);
    ThriftFoo foo1;
    foo1.a().value() = 1;
    foo1.b().value() = 2;
    foo1.c().value() = 3;

    ThriftFoo2 foo2;
    foo2.d().value() = 4;
    foo2.e().value() = 5;
    foo2.f().value() = 6;

    auto objectSize1 = 1000;
    auto objectSize2 = 500;
    auto ttlSecs = 10;

    size_t threadsCount = 10;

    ObjectCacheConfig config;
    config.setCacheName("test")
        .setCacheCapacity(10'000 /*l1EntriesLimit*/)
        .setItemDestructor([&](ObjectCacheDestructorData data) {
          if (data.key == "Foo1") {
            data.deleteObject<ThriftFoo>();
          } else {
            data.deleteObject<ThriftFoo2>();
          }
        })
        .enablePersistence(
            threadsCount, persistBaseFilePath,
            [&](typename ObjectCache::Serializer serializer) {
              if (serializer.key == "Foo1") {
                return serializer.template serialize<ThriftFoo>();
              } else {
                return serializer.template serialize<ThriftFoo2>();
              }
            },
            [&](typename ObjectCache::Deserializer deserializer) {
              if (deserializer.key == "Foo1") {
                return deserializer.template deserialize<ThriftFoo>();
              } else {
                return deserializer.template deserialize<ThriftFoo2>();
              }
            });
    config.objectSizeTrackingEnabled = true;

    {
      auto objcache = ObjectCache::create(config);

      auto object1 = std::make_unique<ThriftFoo>(foo1);
      auto object2 = std::make_unique<ThriftFoo2>(foo2);
      objcache->insertOrReplace("Foo1", std::move(object1), objectSize1,
                                ttlSecs);
      objcache->insertOrReplace("Foo2", std::move(object2), objectSize2);
      ASSERT_EQ(objcache->persist(), true);
    }

    // No objects should expire
    {
      auto objcache = ObjectCache::create(config);
      ASSERT_EQ(objcache->recover(), true);

      auto found1 = objcache->template find<ThriftFoo>("Foo1");
      ASSERT_NE(nullptr, found1);
      EXPECT_EQ(1, found1->a_ref());
      EXPECT_EQ(2, found1->b_ref());
      EXPECT_EQ(3, found1->c_ref());
      auto found2 = objcache->template find<ThriftFoo2>("Foo2");
      ASSERT_NE(nullptr, found2);
      EXPECT_EQ(4, found2->d_ref());
      EXPECT_EQ(5, found2->e_ref());
      EXPECT_EQ(6, found2->f_ref());

      EXPECT_EQ(objectSize1 + objectSize2, objcache->getTotalObjectSize());
    }

    // Let Foo1 expire
    std::this_thread::sleep_for(std::chrono::seconds{15});
    {
      auto objcache = ObjectCache::create(config);
      ASSERT_EQ(objcache->recover(), true);

      auto found1 = objcache->template find<ThriftFoo>("Foo1");
      ASSERT_EQ(nullptr, found1);

      auto found2 = objcache->template find<ThriftFoo2>("Foo2");
      ASSERT_NE(nullptr, found2);
      EXPECT_EQ(4, found2->d_ref());
      EXPECT_EQ(5, found2->e_ref());
      EXPECT_EQ(6, found2->f_ref());
      EXPECT_EQ(objectSize2, objcache->getTotalObjectSize());
    }

    // test recover failure
    {
      config.enablePersistence(
          threadsCount, "random_path",
          [&](typename ObjectCache::Serializer serializer) {
            if (serializer.key == "Foo1") {
              return serializer.template serialize<ThriftFoo>();
            } else {
              return serializer.template serialize<ThriftFoo2>();
            }
          },
          [&](typename ObjectCache::Deserializer deserializer) {
            if (deserializer.key == "Foo1") {
              return deserializer.template deserialize<ThriftFoo>();
            } else {
              return deserializer.template deserialize<ThriftFoo2>();
            }
          });
      auto objcache = ObjectCache::create(config);
      ASSERT_EQ(objcache->recover(), false);
    }
    // test different thread count won't fail recover
    {
      config.enablePersistence(
          threadsCount - 2, persistBaseFilePath,
          [&](typename ObjectCache::Serializer serializer) {
            if (serializer.key == "Foo1") {
              return serializer.template serialize<ThriftFoo>();
            } else {
              return serializer.template serialize<ThriftFoo2>();
            }
          },
          [&](typename ObjectCache::Deserializer deserializer) {
            if (deserializer.key == "Foo1") {
              return deserializer.template deserialize<ThriftFoo>();
            } else {
              return deserializer.template deserialize<ThriftFoo2>();
            }
          });

      auto objcache = ObjectCache::create(config);
      ASSERT_EQ(objcache->recover(), true);
      auto found = objcache->template find<ThriftFoo2>("Foo2");
      ASSERT_NE(nullptr, found);
      EXPECT_EQ(4, found->d_ref());
      EXPECT_EQ(5, found->e_ref());
      EXPECT_EQ(6, found->f_ref());
      EXPECT_EQ(objectSize2, objcache->getTotalObjectSize());
    }
  }

  void testMultithreadReplace() {
    // Sanity test to see if insertOrReplace across multiple
    // threads are safe.
    ObjectCacheConfig config;
    config.setCacheName("test").setCacheCapacity(10'000).setItemDestructor(
        [&](ObjectCacheDestructorData data) { data.deleteObject<Foo>(); });
    auto objcache = ObjectCache::create(config);

    auto runReplaceOps = [&] {
      for (int i = 0; i < 2000; i++) {
        // Rotate through 5 different keys
        auto key = folly::sformat("key_{}", i % 5);
        auto foo2 = std::make_unique<Foo>();
        objcache->insertOrReplace(key, std::move(foo2));
      }
    };

    std::vector<std::thread> ts;
    for (int i = 0; i < 10; i++) {
      ts.push_back(std::thread{runReplaceOps});
    }
    for (int i = 0; i < 10; i++) {
      ts[i].join();
    }
  }

  void testMultithreadEviction() {
    // Sanity test to see if evictions across multiple
    // threads are safe.
    ObjectCacheConfig config;
    config.setCacheName("test").setCacheCapacity(1000).setItemDestructor(
        [&](ObjectCacheDestructorData data) { data.deleteObject<Foo>(); });
    auto objcache = ObjectCache::create(config);

    auto runInsertOps = [&](int id) {
      for (int i = 0; i < 2000; i++) {
        auto key = folly::sformat("key_{}_{}", id, i);
        auto foo2 = std::make_unique<Foo>();
        objcache->insertOrReplace(key, std::move(foo2));
      }
    };

    std::vector<std::thread> ts;
    for (int i = 0; i < 10; i++) {
      ts.push_back(std::thread{runInsertOps, i});
    }
    for (int i = 0; i < 10; i++) {
      ts[i].join();
    }
  }

  void testMultithreadSizeControl() {
    ObjectCacheConfig config;
    config.setCacheName("test")
        .setCacheCapacity(200 /* l1EntriesLimit*/, 100000 /* cacheSizeLimit */,
                          100 /* sizeControllerIntervalMs */)
        .setItemDestructor(
            [&](ObjectCacheDestructorData data) { data.deleteObject<Foo>(); });

    auto objcache = ObjectCache::create(config);

    auto runInsertOps = [&](int id) {
      for (int i = 0; i < 2000; i++) {
        auto key = folly::sformat("key_{}_{}", id, i);
        auto foo2 = std::make_unique<Foo>();
        objcache->insertOrReplace(key, std::move(foo2), 1000);
      }
      // give enough time for size controller to process
      std::this_thread::sleep_for(std::chrono::milliseconds{200});
    };

    std::vector<std::thread> ts;
    for (int i = 0; i < 10; i++) {
      ts.push_back(std::thread{runInsertOps, i});
    }
    for (int i = 0; i < 10; i++) {
      ts[i].join();
    }

    EXPECT_EQ(objcache->getCurrentEntriesLimit(), 100);
  }

  void testMultithreadFindAndReplace() {
    // Sanity test to see if find and insertions at the same time
    // across mutliple threads are safe.
    ObjectCacheConfig config;
    config.setCacheName("test").setCacheCapacity(10'000).setItemDestructor(
        [&](ObjectCacheDestructorData data) { data.deleteObject<Foo>(); });
    auto objcache = ObjectCache::create(config);

    auto runReplaceOps = [&] {
      for (int i = 0; i < 2000; i++) {
        // Rotate through 5 different keys
        auto key = folly::sformat("key_{}", i % 5);
        auto foo2 = std::make_unique<Foo>();
        objcache->insertOrReplace(key, std::move(foo2));
      }
    };

    auto runFindOps = [&] {
      for (int i = 0; i < 2000; i++) {
        // Rotate through 5 different keys
        auto key = folly::sformat("key_{}", i % 5);
        auto res = objcache->template find<Foo>(key);
      }
    };

    std::vector<std::thread> rs;
    std::vector<std::thread> fs;
    for (int i = 0; i < 10; i++) {
      rs.push_back(std::thread{runReplaceOps});
      fs.push_back(std::thread{runFindOps});
    }
    for (int i = 0; i < 10; i++) {
      rs[i].join();
      fs[i].join();
    }
  }

  void testMultithreadFindAndEviction() {
    // Sanity test to see if find and evictions across multiple
    // threads are safe.
    ObjectCacheConfig config;
    config.setCacheName("test").setCacheCapacity(1000).setItemDestructor(
        [&](ObjectCacheDestructorData data) { data.deleteObject<Foo>(); });
    auto objcache = ObjectCache::create(config);

    auto runInsertOps = [&](int id) {
      for (int i = 0; i < 2000; i++) {
        auto key = folly::sformat("key_{}_{}", id, i);
        auto foo2 = std::make_unique<Foo>();
        objcache->insertOrReplace(key, std::move(foo2));
      }
    };

    auto runFindOps = [&](int id) {
      for (int i = 0; i < 2000; i++) {
        auto key = folly::sformat("key_{}_{}", id, i);
        auto res = objcache->template find<Foo>(key);
      }
    };

    std::vector<std::thread> rs;
    std::vector<std::thread> fs;
    for (int i = 0; i < 10; i++) {
      rs.push_back(std::thread{runInsertOps, i});
      fs.push_back(std::thread{runFindOps, i});
    }
    for (int i = 0; i < 10; i++) {
      rs[i].join();
      fs[i].join();
    }
  }

  void testMultithreadFindAndReplaceWith10Shards() {
    // Sanity test to see if find and evictions across multiple
    // threads are safe.
    ObjectCacheConfig config;
    config.setCacheName("test")
        .setCacheCapacity(100'000)
        .setNumShards(10)
        .setItemDestructor(
            [&](ObjectCacheDestructorData data) { data.deleteObject<Foo>(); });
    auto objcache = ObjectCache::create(config);

    auto runReplaceOps = [&] {
      for (int i = 0; i < 2000; i++) {
        // Rotate through 5 different keys
        auto key = folly::sformat("key_{}", i % 5);
        auto foo2 = std::make_unique<Foo>();
        objcache->insertOrReplace(key, std::move(foo2));
      }
    };

    auto runFindOps = [&] {
      for (int i = 0; i < 2000; i++) {
        // Rotate through 5 different keys
        auto key = folly::sformat("key_{}", i % 5);
        auto res = objcache->template find<Foo>(key);
      }
    };

    std::vector<std::thread> rs;
    std::vector<std::thread> fs;
    for (int i = 0; i < 10; i++) {
      rs.push_back(std::thread{runReplaceOps});
      fs.push_back(std::thread{runFindOps});
    }
    for (int i = 0; i < 10; i++) {
      rs[i].join();
      fs[i].join();
    }
  }
};

using AllocatorTypes = ::testing::Types<LruAllocator,
                                        Lru2QAllocator,
                                        TinyLFUAllocator,
                                        LruAllocatorSpinBuckets>;
TYPED_TEST_CASE(ObjectCacheTest, AllocatorTypes);
TYPED_TEST(ObjectCacheTest, GetAllocSize) { this->testGetAllocSize(); }
TYPED_TEST(ObjectCacheTest, ConfigValidation) { this->testConfigValidation(); }
TYPED_TEST(ObjectCacheTest, Simple) { this->testSimple(); }
TYPED_TEST(ObjectCacheTest, MultiType) { this->testMultiType(); }
TYPED_TEST(ObjectCacheTest, testMultiTypePolymorphism) {
  this->testMultiTypePolymorphism();
}
TYPED_TEST(ObjectCacheTest, UserItemDestructor) {
  this->testUserItemDestructor();
}
TYPED_TEST(ObjectCacheTest, Expiration) { this->testExpiration(); }
TYPED_TEST(ObjectCacheTest, Replace) { this->testReplace(); }
TYPED_TEST(ObjectCacheTest, UniqueInsert) { this->testUniqueInsert(); }
TYPED_TEST(ObjectCacheTest, ObjectSizeTrackingBasics) {
  this->testObjectSizeTrackingBasics();
}
TYPED_TEST(ObjectCacheTest, ObjectSizeTrackingUniqueInsert) {
  this->testObjectSizeTrackingUniqueInsert();
}
TYPED_TEST(ObjectCacheTest, Persistence) { this->testPersistence(); }
TYPED_TEST(ObjectCacheTest, PersistenceMultiType) {
  this->testPersistenceMultiType();
}

TYPED_TEST(ObjectCacheTest, MultithreadReplace) {
  this->testMultithreadReplace();
}
TYPED_TEST(ObjectCacheTest, MultithreadEviction) {
  this->testMultithreadEviction();
}
TYPED_TEST(ObjectCacheTest, MultithreadSizeControl) {
  this->testMultithreadSizeControl();
}
TYPED_TEST(ObjectCacheTest, MultithreadFindAndReplace) {
  this->testMultithreadFindAndReplace();
}
TYPED_TEST(ObjectCacheTest, MultithreadFindAndEviction) {
  this->testMultithreadFindAndEviction();
}
TYPED_TEST(ObjectCacheTest, MultithreadFindAndReplaceWith10Shards) {
  this->testMultithreadFindAndReplaceWith10Shards();
}

using ObjectCache = ObjectCache<LruAllocator>;
TEST(ObjectCacheTest, LruEviction) {
  ObjectCache::Config config;
  config.setCacheName("test").setCacheCapacity(1024);
  config.setItemDestructor(
      [&](ObjectCacheDestructorData data) { data.deleteObject<Foo>(); });
  auto objcache = ObjectCache::create(config);

  for (int i = 0; i < 1025; i++) {
    auto foo = std::make_unique<Foo>();
    foo->a = i;
    auto key = folly::sformat("key_{}", i);
    objcache->insertOrReplace(key, std::move(foo));
    auto found = objcache->find<Foo>(key);
    ASSERT_NE(nullptr, found);
    EXPECT_EQ(i, found->a);
  }
  auto found = objcache->find<Foo>("key_0");
  EXPECT_EQ(nullptr, found);
}

TEST(ObjectCacheTest, LruEvictionWithSizeControl) {
  ObjectCache::Config config;
  config.setCacheName("test");
  config.setItemDestructor(
      [&](ObjectCacheDestructorData data) { data.deleteObject<Foo>(); });
  config.setCacheCapacity(50 /* l1EntriesLimit*/, 100 /* cacheSizeLimit */,
                          100 /* sizeControllerIntervalMs */);
  // insert objects with equal size
  {
    auto objcache = ObjectCache::create(config);
    for (size_t i = 0; i < 5; i++) {
      auto key = folly::sformat("key_{}", i);
      objcache->insertOrReplace(key, std::make_unique<Foo>(), 25);
    }
    ASSERT_EQ(objcache->getTotalObjectSize(), 125);
    ASSERT_EQ(objcache->getCurrentEntriesLimit(), config.l1EntriesLimit);
    ASSERT_EQ(objcache->getNumEntries(), 5);
    // wait for size controller
    std::this_thread::sleep_for(std::chrono::milliseconds{150});
    // key_0 should be evicted
    ASSERT_EQ(objcache->getTotalObjectSize(), 100);
    ASSERT_EQ(objcache->getCurrentEntriesLimit(), 4);
    ASSERT_EQ(objcache->getNumEntries(), 4);
    EXPECT_EQ(nullptr, objcache->find<Foo>("key_0"));
    EXPECT_NE(nullptr, objcache->find<Foo>("key_1"));
    EXPECT_NE(nullptr, objcache->find<Foo>("key_2"));
    EXPECT_NE(nullptr, objcache->find<Foo>("key_3"));
    EXPECT_NE(nullptr, objcache->find<Foo>("key_4"));
  }

  // insert and then access objects from the tail
  {
    auto objcache = ObjectCache::create(config);
    for (size_t i = 0; i < 10; i++) {
      auto key = folly::sformat("key_{}", i);
      objcache->insertOrReplace(key, std::make_unique<Foo>(), 25);
    }
    // access key_0 ~ key_3 from the tail
    objcache->find<Foo>("key_0");
    objcache->find<Foo>("key_1");
    objcache->find<Foo>("key_2");
    objcache->find<Foo>("key_3");
    ASSERT_EQ(objcache->getTotalObjectSize(), 250);
    ASSERT_EQ(objcache->getCurrentEntriesLimit(), config.l1EntriesLimit);
    ASSERT_EQ(objcache->getNumEntries(), 10);
    // wait for size controller
    std::this_thread::sleep_for(std::chrono::milliseconds{150});
    // key_0 ~ key_3 should be kept
    ASSERT_EQ(objcache->getTotalObjectSize(), 100);
    ASSERT_EQ(objcache->getCurrentEntriesLimit(), 4);
    ASSERT_EQ(objcache->getNumEntries(), 4);
    EXPECT_NE(nullptr, objcache->find<Foo>("key_0"));
    EXPECT_NE(nullptr, objcache->find<Foo>("key_1"));
    EXPECT_NE(nullptr, objcache->find<Foo>("key_2"));
    EXPECT_NE(nullptr, objcache->find<Foo>("key_3"));
    EXPECT_EQ(nullptr, objcache->find<Foo>("key_4"));
    EXPECT_EQ(nullptr, objcache->find<Foo>("key_5"));
    EXPECT_EQ(nullptr, objcache->find<Foo>("key_6"));
    EXPECT_EQ(nullptr, objcache->find<Foo>("key_7"));
    EXPECT_EQ(nullptr, objcache->find<Foo>("key_8"));
    EXPECT_EQ(nullptr, objcache->find<Foo>("key_9"));
  }

  // insert objects with different sizes
  {
    auto objcache = ObjectCache::create(config);
    for (size_t i = 0; i < 10; i++) {
      auto key = folly::sformat("key_{}", i);
      objcache->insertOrReplace(key, std::make_unique<Foo>(), 25 + i);
    }
    ASSERT_EQ(objcache->getTotalObjectSize(), 295);
    ASSERT_EQ(objcache->getCurrentEntriesLimit(), config.l1EntriesLimit);
    ASSERT_EQ(objcache->getNumEntries(), 10);
    // wait for size controller
    std::this_thread::sleep_for(std::chrono::milliseconds{150});
    // key_0 ~ key_6 should be evicted
    ASSERT_EQ(objcache->getTotalObjectSize(), 99);
    ASSERT_EQ(objcache->getCurrentEntriesLimit(), 3);
    ASSERT_EQ(objcache->getNumEntries(), 3);
    EXPECT_EQ(nullptr, objcache->find<Foo>("key_0"));
    EXPECT_EQ(nullptr, objcache->find<Foo>("key_1"));
    EXPECT_EQ(nullptr, objcache->find<Foo>("key_2"));
    EXPECT_EQ(nullptr, objcache->find<Foo>("key_3"));
    EXPECT_EQ(nullptr, objcache->find<Foo>("key_4"));
    EXPECT_EQ(nullptr, objcache->find<Foo>("key_5"));
    EXPECT_EQ(nullptr, objcache->find<Foo>("key_6"));
    EXPECT_NE(nullptr, objcache->find<Foo>("key_7"));
    EXPECT_NE(nullptr, objcache->find<Foo>("key_8"));
    EXPECT_NE(nullptr, objcache->find<Foo>("key_9"));
  }
}
} // namespace test
} // namespace objcache2
} // namespace cachelib
} // namespace facebook
