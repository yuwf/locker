#ifndef _LOCKER_H_
#define _LOCKER_H_

// by git@github.com:yuwf/locker.git

#include <string>
#include <map>
#include <unordered_map>
#include <atomic>

// 需要依赖Clock git@github.com:yuwf/clock.git
#include "Clock.h"

// 读写锁
// 注意：读写锁不能递归调用
#if (defined(__cplusplus) && __cplusplus >= 201703L) || (defined(_MSVC_LANG) && _MSVC_LANG >= 201402L)
#include <shared_mutex>
typedef std::shared_mutex shared_mutex;
typedef std::shared_lock<std::shared_mutex> read_lock;
typedef std::unique_lock<std::shared_mutex> write_lock;
#else
#include <boost/thread/locks.hpp>
#include <boost/thread/shared_mutex.hpp>
typedef boost::shared_mutex shared_mutex;
typedef boost::shared_lock<boost::shared_mutex> read_lock;
typedef boost::unique_lock<boost::shared_mutex> write_lock;
#endif

// 加锁数据
struct LockerData
{
	std::atomic<int64_t> lockcount = { 0 };		// 加锁次数
	std::atomic<int64_t> lockwaitTSC = { 0 };	// 加锁等待tsc
	std::atomic<int64_t> lockwaitMaxTSC = { 0 };
	std::atomic<int64_t> lockedTSC = { 0 };		// 加锁tsc 针对加锁成功的
	std::atomic<int64_t> lockedMaxTSC = { 0 };
};

// 加锁位置
struct LockerPosition
{
	const char* name = NULL;	// 加锁位置名 要求是字面变量或者常量静态区数据
	int num = 0;				// 加锁位置号

	LockerPosition(const char* _name_, int _num_) : name(_name_), num(_num_)
	{
	}
	bool operator == (const LockerPosition& other) const
	{
		return name == other.name && num == other.num;
	}
	struct Hash
	{
		uintptr_t operator()(const LockerPosition& obj) const
		{
			return (uintptr_t)obj.name;
		}
	};
};

// 加锁数据记录
class LockerRecord
{
public:
	LockerRecord();

	// 注册加锁点 返回该位置的加锁数据
	LockerData* Reg(const LockerPosition& pos);

	// 快照数据
	// 【参数metricsprefix和tags 不要有相关格式禁止的特殊字符 内部不对这两个参数做任何格式转化】
	// metricsprefix指标名前缀 内部产生指标如下，不包括[]
	// [metricsprefix]locker_lockcount 加锁次数
	// [metricsprefix]locker_lockwait 加锁等待的时间 微秒
	// [metricsprefix]locker_maxlockwait 加锁等待的最大时间 微秒
	// [metricsprefix]locker_locked 加锁的时间 微秒
	// [metricsprefix]locker_maxlocked 加锁的最大时间 微秒
	// tags额外添加的标签， 内部产生标签 name:加锁名称 num;加锁号
	enum SnapshotType { Json, Influx, Prometheus };
	std::string Snapshot(SnapshotType type, const std::string& metricsprefix = "", const std::map<std::string, std::string>& tags = std::map<std::string, std::string>());

	void SetRecord(bool b) { brecord = b; }

protected:
	// 记录的加锁数据
	typedef std::unordered_map<LockerPosition, LockerData*, LockerPosition::Hash> LockerDataMap;
	shared_mutex mutex;
	LockerDataMap records;

	// 是否记录加锁数据
	bool brecord = true;

	// Record自身内部加锁记录的加锁数据
	LockerData* preadexist = NULL;
	LockerData* preadsnapshot = NULL;
	LockerData* pwrite = NULL;

private:
	// 禁止拷贝
	LockerRecord(const LockerRecord&) = delete;
	LockerRecord& operator=(const LockerRecord&) = delete;
};
extern LockerRecord g_lockerrecord;

template<class LockType>
class Locker
{
public:
	// _name_ 参数必须是一个字面量
	template <class Mutex>
	Locker(Mutex& mtx, const char* _name_, int _num_)
		: plockerdata(g_lockerrecord.Reg(LockerPosition(_name_, _num_)))
		, lockwaitTSC(TSC())
		, locker(mtx)
		, lockedTSC(TSC())
	{
	}
	template <class Mutex>
	Locker(Mutex& mtx, LockerData* p)
		: plockerdata(p)
		, lockwaitTSC(TSC())
		, locker(mtx)
		, lockedTSC(TSC())
	{
	}

	~Locker()
	{
		if (plockerdata)
		{
			int64_t unlock = TSC();

			int64_t tsc = lockedTSC - lockwaitTSC;
			plockerdata->lockcount++;
			plockerdata->lockwaitTSC += tsc;
			if (plockerdata->lockwaitMaxTSC < tsc)
			{
				plockerdata->lockwaitMaxTSC = tsc;
			}
			tsc = unlock - lockedTSC;
			plockerdata->lockedTSC += tsc;
			if (plockerdata->lockedMaxTSC < tsc)
			{
				plockerdata->lockedMaxTSC = tsc;
			}
		}
	}

private:
	LockerData* plockerdata = NULL;
	int64_t lockwaitTSC = 0;	// 开始加锁时CPU频率值 必须在locker之前构造 并且定义放到plockerdata后面 防止构造时g_lockerrecord.Reg影响真正加锁的区域
	LockType locker;
	int64_t lockedTSC = 0;		// 加锁成功时CPU频率值 必须在locker之后构造
};

#define ___LockerDataName(line)  lockerdata_##line
#define __LockerDataName(line)  ___LockerDataName(line)
#define _LockerDataName() __LockerDataName(__LINE__)

#define ___LockerObjName(line)  lockerobj_##line
#define __LockerObjName(line)  ___LockerObjName(line)
#define _LockerObjName() __LockerObjName(__LINE__)

// 使用函数名和行号作为参数
#define LockerObject(mutex,locktype) \
	static LockerData* _LockerDataName() = g_lockerrecord.Reg(LockerPosition(__FUNCTION__, __LINE__)); \
	Locker<locktype> _LockerObjName()(mutex, _LockerDataName());

#define WRITE_LOCK(mutex) LockerObject(mutex,write_lock)
#define READ_LOCK(mutex) LockerObject(mutex,read_lock)

#define STD_LOCK_GUARD(mutex) LockerObject(mutex,std::lock_guard<decltype(mutex)>)
#define STD_UNIQUE_LOCK(mutex) LockerObject(mutex,std::unique_lock<decltype(mutex)>)
#define STD_SHARED_LOCK(mutex) LockerObject(mutex,std::shared_lock<decltype(mutex)>)

#define BOOST_MUTEX_SCOPED_LOCK(mutex) LockerObject(mutex,boost::mutex::scoped_lock)
#define BOOST_RECURSIVE_SCOPED_LOCK(mutex) LockerObject(mutex,boost::recursive_mutex::scoped_lock)
#define BOOST_UNIQUE_LOCK(mutex) LockerObject(mutex,boost::unique_lock<decltype(mutex)>)
#define BOOST_SHARED_LOCK(mutex) LockerObject(mutex,boost::shared_lock<decltype(mutex)>)

#endif