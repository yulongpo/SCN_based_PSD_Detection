#include "radioai/icd/HQSigMF.hpp"

/**
 * @brief HQSigMF 域枚举、字符串和频域 Section 的转换工具。
 *
 * 所有函数都是头文件内的 static 实现，适合轻量查询；未知域名转换为
 * Domain::UNKNOWN，找不到频率类别时返回空 shared_ptr。
 */
namespace HQSigMFUtils {
	/**
	* @brief：域转化，实现从字符串转为枚举
	* @param[in] str_domain：字符串域名
	* @return 枚举
	*/
	/// 将 TIME/FREQUENCY/OBJECTS/FEATURE 字符串转换为域枚举。
	static Domain toDomain(const std::string& str_domain)
	{
		Domain result = ("TIME" == str_domain) ? Domain::TIME :
			("FREQUENCY" == str_domain) ? Domain::FREQUENCY :
			("OBJECTS" == str_domain) ? Domain::OBJECTS :
			("FEATURE" == str_domain) ? Domain::FEATURE : Domain::UNKNOWN;
		return result;
	}

	/// 将域枚举转换回稳定的协议字符串，未知值返回 UNKNOWN。
	static const std::string domainToStr(Domain domain)
	{
		std::string result = (Domain::TIME == domain) ? "TIME" :
			(Domain::FREQUENCY == domain) ? "FREQUENCY" :
			(Domain::OBJECTS == domain) ? "OBJECTS" :
			(Domain::FEATURE == domain) ? "FEATURE" : "UNKNOWN";
		return result;
	}

	/**
	* @brief：域转化，实现从字符串列表转为枚举vector列表
	* @param[in] str_domains：字符串域名列表
	* @param[in] delimiter：分隔符
	* @return 枚举列表
	*/
	/// 按 delimiter 切分域列表；空项也会转换为 UNKNOWN 并保留在结果中。
	static std::vector<Domain> toDomains(const std::string& str_domains, const char delimiter = ';')
	{
		std::vector<Domain> result;

		//字符产切割
		std::string str_domain;
		size_t start = 0;
		size_t end = str_domains.find(delimiter);

		while (end != std::string::npos) {

			str_domain = str_domains.substr(start, end - start);
			result.push_back(HQSigMFUtils::toDomain(str_domain));

			start = end + 1;
			end = str_domains.find(delimiter, start);
		}

		// 添加最后一个元素
		str_domain = str_domains.substr(start);
		result.push_back(HQSigMFUtils::toDomain(str_domain));

		return result;
	}

	/**
	* @brief：获取指定子类的频域section
	* @param[in] sig_mf：信号元对象
	* @param[in] category：子类别
	* @return section对象
	*/
	/// 在 HQSigMF 的 FREQUENCY 域中查找第一个匹配二级类别的 Section。
	static std::shared_ptr<HQSigMF::Section> getFreqSection(HQSigMF* sig_mf, FrequencyCategory category)
	{
		std::shared_ptr<HQSigMF::Section> freq_section = nullptr;
		auto freq_sections = sig_mf->getSections(Domain::FREQUENCY);
		for (auto item_sec : freq_sections)
		{
			if (static_cast<FrequencyCategory>(item_sec->property.category) == category)
			{
				freq_section = item_sec;
				break;
			}
		}
		return freq_section;
	}
}
