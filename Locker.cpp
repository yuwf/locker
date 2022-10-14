#include <iosfwd>
#include "Locker.h"

LockerRecord g_lockerrecord;

LockerRecord::LockerRecord()
{
	preadexist = Reg(LockerPosition("LockerRecord::Exist", 0));
	preadsnapshot = Reg(LockerPosition("LockerRecord::Snapshot", 0));
	pwrite = Reg(LockerPosition("LockerRecord::Write", 0));
}

LockerData* LockerRecord::Reg(const LockerPosition& pos)
{
	if (!brecord)
	{
		return NULL;
	}

	// 先用共享锁 如果存在直接修改
	{
		Locker<read_lock> lock(mutex, preadexist);
		auto it = ((const LockerDataMap&)records).find(pos); // 显示的调用const的find
		if (it != records.cend())
		{
			return it->second;
		}
	}

	// 不存在构造一个
	LockerData* p = new LockerData;
	// 直接用写锁
	{
		Locker<write_lock> lock(mutex, pwrite);
		records.insert(std::make_pair(pos, p));
	}
	return p;
}

std::string LockerRecord::Snapshot(SnapshotType type, const std::string& metricsprefix, const std::map<std::string, std::string>& tags)
{
	LockerDataMap lastdata;
	{
		Locker<read_lock> lock(mutex, preadsnapshot);
		lastdata = records;
	}

	static const int metirs_num = 5;
	static const char* metirs[metirs_num] = 
	{ 
		"locker_lockcount",
		"locker_lockwait", "locker_maxlockwait",
		"locker_locked", "locker_maxlocked"
	};

	std::ostringstream ss;
	if (type == Json)
	{
		ss << "[";
		int index = 0;
		for (const auto& it : lastdata)
		{
			int64_t value[metirs_num] = { it.second->lockcount, it.second->lockwaitTSC / TSCPerUS(), it.second->lockwaitMaxTSC / TSCPerUS(), it.second->lockedTSC / TSCPerUS(), it.second->lockedMaxTSC / TSCPerUS() };
			ss << ((++index) == 1 ? "[" : ",[");
			for (int i = 0; i < metirs_num; ++i)
			{
				ss << (i == 0 ? "{" : ",{");
				ss << "\"metrics\":\"" << metricsprefix << metirs[i] << "\",";
				ss << "\"name\":\"" << it.first.name << "\",";
				ss << "\"num\":" << it.first.num << ",";
				for (const auto& it : tags)
				{
					ss << "\"" << it.first << "\":\"" << it.second << "\",";
				}
				ss << "\"value\":" << value[i] << "";
				ss << "}";
			}
			ss << "]";
		}
		ss << "]";
	}
	else if (type == Influx)
	{
		std::string tag;
		for (const auto& t : tags)
		{
			tag += ("," + t.first + "=" + t.second);
		}
		for (const auto& it : lastdata)
		{
			int64_t value[metirs_num] = { it.second->lockcount, it.second->lockwaitTSC / TSCPerUS(), it.second->lockwaitMaxTSC / TSCPerUS(), it.second->lockedTSC / TSCPerUS(), it.second->lockedMaxTSC / TSCPerUS() };
			for (int i = 0; i < metirs_num; ++i)
			{
				ss << metricsprefix << metirs[i];
				ss << ",name=" << it.first.name << ",num=" << it.first.num << tag;
				ss << " value=" << value[i] << "i\n";
			}
		}
	}
	else if (type == Prometheus)
	{
		std::string tag;
		for (const auto& t : tags)
		{
			tag += ("," + t.first + "=\"" + t.second + "\"");
		}
		for (const auto& it : lastdata)
		{
			int64_t value[metirs_num] = { it.second->lockcount, it.second->lockwaitTSC / TSCPerUS(), it.second->lockwaitMaxTSC / TSCPerUS(), it.second->lockedTSC / TSCPerUS(), it.second->lockedMaxTSC / TSCPerUS() };
			for (int i = 0; i < metirs_num; ++i)
			{
				ss << metricsprefix << metirs[i];
				ss << "{name=\"" << it.first.name << "\",num=\"" << it.first.num << "\"" << tag << "}";
				ss << " " << value[i] << "\n";
			}
		}
	}
	return ss.str();
}
