#pragma once
#ifdef _WIN64
#define _CRT_NONSTDC_NO_DEPRECATE 1
#define _CRT_NONSTDC_NO_WARNINGS   1
#define _CRT_SECURE_NO_WARNINGS 1
#include <io.h>
#else
#include <unistd.h>
#endif
#include <iostream>
using namespace std;
#include <stdio.h>
#include <vector>
#include <string>
#include <fcntl.h>
#include <memory.h>
#include <assert.h>
#include <thread>
#include <mutex>
#include <sys/mman.h>
using namespace std;

class CRC64Coder {
public:
    CRC64Coder() {
        init();
    }

    static unsigned long long calc(const void* data, size_t size) {
        initTable();
        const unsigned char *p = reinterpret_cast<const unsigned char *>(data);
        unsigned long long ctmp = 0xFFFFFFFFFFFFFFFFULL;
        while (size) {
            ctmp ^= (unsigned long long)(p[0]);
            ctmp = (ctmp >> 8) ^ _table[ctmp & 0xFF];
            p++;
            size--;
        }
        return ctmp ^ 0xFFFFFFFFFFFFFFFFULL;
    }

    void init() {
        initTable();
        _value = 0xFFFFFFFFFFFFFFFFU;
    }

    void update(unsigned char p) {
        _value ^= (unsigned long long)(p);
        _value = (_value >> 8) ^ _table[_value & 0xFF];
    }

    unsigned long long final() {
        return _value ^ 0xFFFFFFFFFFFFFFFFU;
    }

private:
    unsigned long long _value;
    static unsigned long long _table[256];
    static void initTable() {
        if (_table[0] == 0) {
            unsigned long long *p = _table;
            int             i, j;
            for (j = 0; j < 256; j++) {
                unsigned long long x = j;
                for (i = 0; i < 8; i++) {
                    if ((x & 1) != 0) {
                        x = (x >> 1) ^ 0xC96C5795D7870F42ULL;
                    } else {
                        x = (x >> 1);
                    }
                }
                *p++ = x;
            }
        }
    }
};

unsigned long long CRC64Coder::_table[256] = { 0 };
static CRC64Coder  c64;


#ifndef O_BINARY
#define O_BINARY 0
#endif

using key = string;
using datum = string;
using ull  = unsigned long long;

static ull primes[] = {1009, 2011, 4013, 8017, 16001, 32003, 64007, 128021, 256019, 512009, 1000003, 1400017, 2000003, 2800001, 4000037,
                      6000011, 8000009, 10000019, 13000027, 16000057, 20000003, 25000009, 30000001, 40000003, 50000017, 60000011, 
				      70000027, 80000023, 90000049, 100000007, 110000017, 120000007, 130000001, 200000033, 300000007, 400000009, 500000003,
				      600000001, 700000001, 800000011, 900000011, 1000000007};

class pht { // persistent hash table
public:
	pht() {
	}
	
	int open(string const &name, bool cached = true) {
        fd = ::open(name.c_str(), O_RDONLY|O_BINARY);
		if (fd < 0) {
			return -1;
		}
        lseek(fd, 0, SEEK_END);
        free_place = lseek(fd, 0, SEEK_CUR);
        free_place = (free_place + 65535) & ~0xFFFF;
        map = (unsigned char *)::mmap(NULL, free_place, PROT_READ, MAP_SHARED, fd, 0);
        printf("pht::open: map=%p\n", map);
        if (map == MAP_FAILED) {
            perror(name.c_str());
            abort();
            map = nullptr;
        }
		ull m;
		if (hread(&m, sizeof m, 0) != sizeof m) {
            ::munmap(map, free_place);
			::close(fd);
			return -2;
		}
		if (m != MAGIC) {
            printf("got %llx instead of %llx\n", m, MAGIC);
            ::munmap(map, free_place);
			::close(fd);
			return -3;
		}
		if (hread(&hsize, sizeof hsize, sizeof m) != sizeof hsize) {
            ::munmap(map, free_place);
			::close(fd);
			return -4;
		}
        if (cached) {
            cache = new ull[hsize];
            printf("open:cache[%llu]=%p\n", hsize, cache);
        }
        if (cache != nullptr) {
            ssize_t rd = hread(cache, sizeof (ull) * hsize, BLOCK_SIZE);
            printf("open:read cache[%llu]: got %zd bytes\n", hsize, rd);
            if (rd < 0) {
                perror("hread");
            }
        }
		const ull POS_PER_BLOCK = BLOCK_SIZE / POS_SIZE;
		ull blocks = (hsize + POS_PER_BLOCK - 1) / POS_PER_BLOCK;
		start_data = BLOCK_SIZE + blocks * BLOCK_SIZE;
		return 0;
	}

    const int MAX_IO_SIZE = 4096*4096;
    ssize_t hwrite(const void *addr, size_t size, off_t off) const {
        if (map == nullptr) {
            ssize_t total = 0;
            while (size > 0) {
                ssize_t towrite = size > MAX_IO_SIZE ? MAX_IO_SIZE : size;
                auto wr = ::pwrite(fd, addr, towrite, off);
                //printf("pwrite(%zd bytes)=%zd\n", towrite, wr);
                if (wr < 0) {
                    return total;
                }
                addr = (const void *)((const char *)addr+wr);
                off += wr;
                size -= wr;
                total += wr;
            }
            return total;
        } else {
            ::memcpy(map + off, addr, size);
            return size;
        }   
    }

    ssize_t hread(void *addr, size_t size, off_t off) const {
        if (map == nullptr) {
            ssize_t total = 0;
            while (size > 0) {
                ssize_t toread = size > MAX_IO_SIZE ? MAX_IO_SIZE : size;
                auto rd = ::pread(fd, addr, toread, off);
                //printf("pread(%zd bytes)=%zd\n", toread, rd);
                if (rd < 0) {
                    return total;
                }
                off += rd;
                addr = (void *)((char *)addr+rd);
                size -= rd;
                total += rd;
            }
            return total;
        } else {
            ::memcpy(addr, map + off, size);
            return size;
        }   
    }

    
	int create(string const &name, size_t capacity, off_t init_size) {
        fd = ::open(name.c_str(), O_RDWR|O_BINARY|O_CREAT, 0600);
		if (fd < 0) {
			return -1;
		}
        init_size = (init_size + 65535) & ~0xFFFF;
        ::pwrite(fd, "", 1, init_size);
        map = (unsigned char *)::mmap(NULL, init_size, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);
        if (map == MAP_FAILED) {
            perror(name.c_str());
            close(fd);
            return -2;
        }
		ull m = MAGIC;
		hwrite(&m, sizeof m, 0);
		hsize = capacity;
		hwrite(&hsize, sizeof hsize, sizeof m);
		const ull POS_PER_BLOCK = BLOCK_SIZE / POS_SIZE;
		size_t blocks = (capacity + POS_PER_BLOCK - 1) / POS_PER_BLOCK;
		char block[BLOCK_SIZE] = {0};
		hwrite(block, BLOCK_SIZE, BLOCK_SIZE); 
		for (ull i = 1; i < blocks+1; i++) {
			hwrite(block, BLOCK_SIZE, BLOCK_SIZE*i);
		}
        cache = new ull[hsize];
        for (size_t i = 0; i < hsize; i++) {
            cache[i] = 0;
        }
        start_data = BLOCK_SIZE + blocks * BLOCK_SIZE;
        free_place = start_data;
        printf("pht::create: map=%p\n", map);
        return 0;
	}

    ull hash(key const &k) {
        auto h = CRC64Coder::calc(k.c_str(), k.size());
        return h % hsize;
    }

	bool insert(key const &k, datum const &d) {
        int lev = 0;
        daisies[lev]++;
        lev++;
        inserts++;
        hash_item hi(k, d);
        auto s = hi.size();
        ull h = hash(k);
        ull pos;
        read_pos(h, pos);
        if (pos == 0) {
            // Нет такой первичной записи
            pos = free_place;
            hi.set_overflow(0);
            write_pos(h, pos);
            write_key(pos, hi);
            free_place = free_place + s;
            return true;
        }
        hash_item t;
        auto old_pos = pos;
        while (pos != 0) {
            if (lev < 10) {
                daisies[lev]++;
                lev++;
            }
            assert(pos < free_place);
            // Такая запись есть, сравниваем ключи
            read_key(pos, t);
            if (t.get_key() == k) {
                // Уже есть и именно с этим ключом.
                printf("key '%s' is already in the table\n", k.c_str());
                return false;
            }
            // Ключ есть, но он другой. Коллизия.  Бежим по цепочке переполнения до последнего.
            old_pos = pos;
            pos = t.overflow();
        }
        // Здесь pos = 0 и в h содержится последний считанный ключ а в old_pos его позиция.
        overflows++;
        pos = free_place;
        t.set_overflow(pos);
        write_key(old_pos, t);
        hi.set_overflow(0);
        write_key(pos, hi);
        free_place = free_place + s;
		return true;
	}

	bool find(key const &k, datum &d) {
        searches++;
        hash_item hi(k, d);
        ull h = hash(k);
        ull pos;
        read_pos(h, pos);
        if (pos == 0) {
            // printf("find(%s): pos=0\n", k.c_str());
            return false;
        }
        hash_item t;
        auto old_pos = pos;
        while (pos != 0) {
            // printf("find(%s): pos=%llu\n", k.c_str(),pos);
            assert(pos < free_place);
            // Такая запись есть, сравниваем ключи
            read_key(pos, t);
            // printf("find(%s): pos=%llu key='%s'\n", k.c_str(),pos, t.get_key().c_str());
            if (t.get_key() == k) {
                d = t.get_data();
                return true;
            }
            // Ключ есть, но он другой. Коллизия.  Бежим по цепочке переполнения до последнего.
            old_pos = pos;
            pos = t.overflow();
        }
        return false;
    }

    using searchResult = pair<ull, ull>;
    enum { NOT_FOUND = 0xFFFFFFFFFFFFFFFFULL };
    searchResult first(key &k) const {
        for (ull i = 0; i < hsize; i++) {
            ull pos;
            read_pos(i, pos);
            if (pos != 0) {
                hash_item hi;
                read_key(pos, hi);
                k = hi.get_key();
                return{ i, hi.overflow() };
            }
        }
        return{ NOT_FOUND,0 };
    }

    searchResult next(searchResult cur, key& k) const {
        ull pos = 0;
        if (cur.second != 0) {
            pos = cur.second;
            hash_item hi;
            read_key(pos, hi);
            k = hi.get_key();
            return{ cur.first, hi.overflow() };
        }
        for (ull i = cur.first+1; i < hsize; i++) {
            read_pos(i, pos);
            if (pos != 0) {
                hash_item hi;
                read_key(pos, hi);
                k = hi.get_key();
                return{ i, hi.overflow() };
            }
        }
        return{ NOT_FOUND,0 };
    }


	void list() const {
 		printf("hash_size = %llu\n", hsize);
		for (ull i = 0; i < hsize; i++) {
			ull pos;
			read_pos(i, pos);
			if (pos != 0) {
                hash_item hi;
                read_key(pos, hi);
				printf("%llu: has data at %llu: %s\n", i, pos, hi.print().c_str());
			}
		}
	}
    ~pht() {
        //list();
        if (map != nullptr) {
            lseek(fd, 0, SEEK_END);
            printf("unmap(%p,%lld)\n", map, lseek(fd,0,SEEK_CUR));
            int code = munmap(map, lseek(fd, 0, SEEK_CUR));
            if (code != 0) {
                perror("munmap");
            } 
            map = nullptr;
        }
        if (fd >= 0) {
            printf("truncate to %llu\n", free_place);
            if (ftruncate(fd, free_place) < 0) {
                perror("ftruncate");
            }
            close(fd);
        }
        if (cache != nullptr) {
            delete[] cache;
        }
#if 0        
        printf("pht: inserts=%llu searches=%llu overflows=%llu pos:reads=%llu writes=%llu, key:reads=%llu writes=%llu\n"
            "daizies={%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu}\n",
            inserts, searches, overflows,
            pos_reads, pos_writes, keys_reads, keys_writes,
            daisies[0],daisies[1],daisies[2],daisies[3],daisies[4],daisies[5],daisies[6],daisies[7],daisies[8],daisies[9]);
#endif
    }
private:
	enum {
		POS_SIZE = 8, // implies crc64
		BLOCK_SIZE = 4096
	};
    unsigned char *map = nullptr;
    mutable unsigned long long inserts = 0;
    mutable unsigned long long overflows = 0;
    mutable unsigned long long keys_reads = 0;
    mutable unsigned long long keys_writes = 0;
    mutable unsigned long long pos_reads = 0;
    mutable unsigned long long pos_writes = 0;
    mutable unsigned long long searches = 0;
    mutable unsigned long long daisies[10] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    enum : unsigned long long {
        MAGIC = 0x778695A4B3C2D1E0ull,
    };

    class hash_item {
    public:
        hash_item(key const &k, datum const &d) {
            ks = k.size() + 1;
            ds = d.size() + 1;
            memcpy(body, k.c_str(), ks);
            memcpy(body + ks, d.c_str(), ds);
            set_overflow(0);
        }
        key get_key() const {
            return body;
        }
        datum get_data() const {
            return body + ks;
        }
        void set_overflow(ull ov) {
            memcpy(body + ks + ds, &ov, sizeof ov);
        }

        hash_item() {

        }
        void init(void *mem, size_t) {
            const char *m = (const char *)mem;
            ks = strlen(m) + 1;
            ds = strlen(m + ks) + 1;
            memcpy(body, mem, ks + ds + sizeof(ull));
        }
        size_t size() const {
            return ks + ds + sizeof(ull);
        }
        void *mem() const {
            return (void *)body;
        }
        ull overflow() const {
            return *(ull *)((char *)body + ks + ds);
        }
        string print() const {
            string ret = "[";
            const char *m = (const char *)body;
            ret += m;
            ret += ",";
            ret += m + ks;
            ret += ",";
            ret += to_string(overflow());
            ret += "]";
            return ret;
        }
        ~hash_item() {
        }
    private:
        char body[20000];
        size_t ks = 0;
        size_t ds = 0;
    };

	ull hsize = 0;
	ull start_data = 0;
    ull free_place = 0;
	int fd = -1;
    ull *cache = nullptr;
		
    void write_key(ull start, hash_item const &hi) {
        keys_writes++;
        hwrite(hi.mem(), (unsigned)hi.size(), start);
        //printf("write_key(%lld):%s\n", start, hi.print().c_str());
    }

    void read_key(ull start, hash_item &hi) const {
        keys_reads++;
        char buf[10240];
        if (map == nullptr) { 
            hread(buf, sizeof buf, start);
            hi.init(buf, sizeof buf);
        } else {
            hi.init(map+start, sizeof buf);
        }
        //printf("read_key(%lld):%s\n", start, hi.print().c_str());
    }

	void read_pos(ull no, ull &num) const {
        pos_reads++;
        if (cache != nullptr) {
            //printf("read_pos[%llu]=%llu (cached)\n", no, cache[no]);
            num = cache[no];
            return;
        }
		hread(&num, sizeof num, BLOCK_SIZE + no * POS_SIZE);
 //       printf("read_pos(%llu)=%llu\n", no, num);
	}
	
	void write_pos(ull no, ull num) const {
        pos_writes++;
		hwrite(&num, sizeof num, BLOCK_SIZE + no * POS_SIZE);
        if (cache != nullptr && cache[no] == 0) {
            cache[no] = num;
        }
//        printf("write_pos(%llu,%llu)\n", no, num);
    }
	
	
};



