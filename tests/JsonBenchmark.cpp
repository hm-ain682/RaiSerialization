// @file JsonBenchmark.cpp
// @brief JSON繝代・繧ｵ繝ｼ縺ｮ繝代ヵ繧ｩ繝ｼ繝槭Φ繧ｹ險域ｸｬ

import rai.json.json_writer;
import rai.json.json_parser;
import rai.json.json_tokenizer;
import rai.json.json_token_manager;
import rai.json.json_binding;
import rai.json.json_io;
import rai.json.parallel_input_stream_source;
import rai.json.reading_ahead_buffer;
#include <gtest/gtest.h>
#include <chrono>
#include <string>
#include <vector>
#include <memory>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <iostream>
#include <cmath>

using namespace rai::json;

// ********************************************************************************
// 繝昴Μ繝｢繝ｼ繝輔ぅ繝・け蝙九・逋ｻ骭ｲ・亥燕譁ｹ螳｣險・・
// ********************************************************************************

struct BaseNode;
struct DataNode;
struct ContainerNode;

// ********************************************************************************
// 繝・せ繝育畑繝・・繧ｿ讒矩
// ********************************************************************************

/// @brief 繝励Μ繝溘ユ繧｣繝門梛縺ｮ縺ｿ繧呈戟縺､蜊倡ｴ斐↑讒矩菴・
struct SimpleData {
    int id = 0;
    double value = 0.0;
    bool flag = false;
    std::string name;

    const IJsonFieldSet& jsonFields() const {
        static const auto fields = makeJsonFieldSet<SimpleData>(
            JsonField<&SimpleData::id>{"id"},
            JsonField<&SimpleData::value>{"value"},
            JsonField<&SimpleData::flag>{"flag"},
            JsonField<&SimpleData::name>{"name"}
        );
        return fields;
    }
};

/// @brief 繝吶け繧ｿ繝ｼ繧貞性繧讒矩菴・
struct VectorData {
    std::string category;
    std::vector<int> numbers;
    std::vector<std::string> tags;

    const IJsonFieldSet& jsonFields() const {
        static const auto fields = makeJsonFieldSet<VectorData>(
            JsonField<&VectorData::category>{"category"},
            JsonField<&VectorData::numbers>{"numbers"},
            JsonField<&VectorData::tags>{"tags"}
        );
        return fields;
    }
};

/// @brief 繝昴Μ繝｢繝ｼ繝輔ぅ繝・け蝙九・蝓ｺ蠎輔け繝ｩ繧ｹ
struct BaseNode {
    std::string type;
    int nodeId = 0;

    virtual ~BaseNode() = default;

    virtual const IJsonFieldSet& jsonFields() const {
        static const auto fields = makeJsonFieldSet<BaseNode>(
            JsonField<&BaseNode::type>{"type"},
            JsonField<&BaseNode::nodeId>{"nodeId"}
        );
        return fields;
    }
};

/// @brief 繝昴Μ繝｢繝ｼ繝輔ぅ繝・け蝙九・豢ｾ逕溘け繝ｩ繧ｹ1
struct DataNode : public BaseNode {
    double dataValue = 0.0;

    const IJsonFieldSet& jsonFields() const override {
        static const auto fields = makeJsonFieldSet<DataNode>(
            JsonField<&BaseNode::type>{"type"},
            JsonField<&BaseNode::nodeId>{"nodeId"},
            JsonField<&DataNode::dataValue>{"dataValue"}
        );
        return fields;
    }
};

/// @brief 繝昴Μ繝｢繝ｼ繝輔ぅ繝・け蝙九・豢ｾ逕溘け繝ｩ繧ｹ2
struct ContainerNode : public BaseNode {
    std::vector<std::string> children;

    const IJsonFieldSet& jsonFields() const override {
        static const auto fields = makeJsonFieldSet<ContainerNode>(
            JsonField<&BaseNode::type>{"type"},
            JsonField<&BaseNode::nodeId>{"nodeId"},
            JsonField<&ContainerNode::children>{"children"}
        );
        return fields;
    }
};

// ComplexData讒矩菴薙・蠕後〒螳夂ｾｩ・医・繝ｪ繝｢繝ｼ繝輔ぅ繝・け繧ｨ繝ｳ繝医Μ縺ｮ蠕鯉ｼ・

// ********************************************************************************
// 繝昴Μ繝｢繝ｼ繝輔ぅ繝・け蝙九・繝・ぅ繧ｹ繝代ャ繝∫匳骭ｲ
// ********************************************************************************

// 繝昴Μ繝｢繝ｼ繝輔ぅ繝・け蝙九・繧ｨ繝ｳ繝医Μ螳夂ｾｩ
constexpr PolymorphicTypeEntry<BaseNode> baseNodeEntries[] = {
    {"DataNode", []() -> std::unique_ptr<BaseNode> { return std::make_unique<DataNode>(); }},
    {"ContainerNode", []() -> std::unique_ptr<BaseNode> { return std::make_unique<ContainerNode>(); }}
};

// ComplexData讒矩菴薙ｒ蜀榊ｮ夂ｾｩ・医・繝ｪ繝｢繝ｼ繝輔ぅ繝・け繝輔ぅ繝ｼ繝ｫ繝峨ｒ菴ｿ逕ｨ・・
struct ComplexData {
    std::string name;
    int level = 0;
    std::unique_ptr<BaseNode> node;
    std::vector<SimpleData> items;
    std::vector<VectorData> collections;

    const IJsonFieldSet& jsonFields() const {
        static const auto fields = makeJsonFieldSet<ComplexData>(
            JsonField<&ComplexData::name>{"name"},
            JsonField<&ComplexData::level>{"level"},
            JsonPolymorphicField<&ComplexData::node, baseNodeEntries>{"node"},
            JsonField<&ComplexData::items>{"items"},
            JsonField<&ComplexData::collections>{"collections"}
        );
        return fields;
    }
};

// ********************************************************************************
// 繝・せ繝医ョ繝ｼ繧ｿ逕滓・
// ********************************************************************************

/// @brief 蟆剰ｦ乗ｨ｡繝・・繧ｿ逕滓・・域焚KB遞句ｺｦ・・
std::string generateSmallJsonData() {
    std::ostringstream oss;
    oss << "{\n";
    oss << "  \"name\": \"SmallDataSet\",\n";
    oss << "  \"level\": 1,\n";
    oss << "  \"node\": {\n";
    oss << "    \"type\": \"DataNode\",\n";
    oss << "    \"nodeId\": 100,\n";
    oss << "    \"dataValue\": 3.14159\n";
    oss << "  },\n";
    oss << "  \"items\": [\n";

    // 10蛟九・繧｢繧､繝・Β繧堤函謌・
    for (int i = 0; i < 10; ++i) {
        oss << "    {\n";
        oss << "      \"id\": " << i << ",\n";
        oss << "      \"value\": " << (i * 1.5) << ",\n";
        oss << "      \"flag\": " << (i % 2 == 0 ? "true" : "false") << ",\n";
        oss << "      \"name\": \"Item" << i << "\"\n";
        oss << "    }" << (i < 9 ? "," : "") << "\n";
    }

    oss << "  ],\n";
    oss << "  \"collections\": [\n";

    // 5蛟九・繧ｳ繝ｬ繧ｯ繧ｷ繝ｧ繝ｳ繧堤函謌・
    for (int i = 0; i < 5; ++i) {
        oss << "    {\n";
        oss << "      \"category\": \"Category" << i << "\",\n";
        oss << "      \"numbers\": [";
        for (int j = 0; j < 5; ++j) {
            oss << (i * 10 + j) << (j < 4 ? ", " : "");
        }
        oss << "],\n";
        oss << "      \"tags\": [";
        for (int j = 0; j < 3; ++j) {
            oss << "\"tag" << (i * 3 + j) << "\"" << (j < 2 ? ", " : "");
        }
        oss << "]\n";
        oss << "    }" << (i < 4 ? "," : "") << "\n";
    }

    oss << "  ]\n";
    oss << "}\n";

    return oss.str();
}

/// @brief 荳ｭ隕乗ｨ｡繝・・繧ｿ逕滓・・域焚逋ｾKB遞句ｺｦ・・
std::string generateMediumJsonData() {
    std::ostringstream oss;
    oss << "{\n";
    oss << "  \"name\": \"MediumDataSet\",\n";
    oss << "  \"level\": 2,\n";
    oss << "  \"node\": {\n";
    oss << "    \"type\": \"ContainerNode\",\n";
    oss << "    \"nodeId\": 200,\n";
    oss << "    \"children\": [";

    // 100蛟九・蟄舌ヮ繝ｼ繝牙錐繧堤函謌・
    for (int i = 0; i < 100; ++i) {
        oss << "\"child_" << i << "\"" << (i < 99 ? ", " : "");
    }
    oss << "]\n";
    oss << "  },\n";
    oss << "  \"items\": [\n";

    // 1000蛟九・繧｢繧､繝・Β繧堤函謌・
    for (int i = 0; i < 1000; ++i) {
        oss << "    {\n";
        oss << "      \"id\": " << i << ",\n";
        oss << "      \"value\": " << (i * 1.234567) << ",\n";
        oss << "      \"flag\": " << (i % 3 == 0 ? "true" : "false") << ",\n";
        oss << "      \"name\": \"Item_" << std::setfill('0') << std::setw(4) << i << "\"\n";
        oss << "    }" << (i < 999 ? "," : "") << "\n";
    }

    oss << "  ],\n";
    oss << "  \"collections\": [\n";

    // 200蛟九・繧ｳ繝ｬ繧ｯ繧ｷ繝ｧ繝ｳ繧堤函謌・
    for (int i = 0; i < 200; ++i) {
        oss << "    {\n";
        oss << "      \"category\": \"Category_" << i << "\",\n";
        oss << "      \"numbers\": [";
        for (int j = 0; j < 20; ++j) {
            oss << (i * 100 + j) << (j < 19 ? ", " : "");
        }
        oss << "],\n";
        oss << "      \"tags\": [";
        for (int j = 0; j < 10; ++j) {
            oss << "\"tag_" << (i * 10 + j) << "\"" << (j < 9 ? ", " : "");
        }
        oss << "]\n";
        oss << "    }" << (i < 199 ? "," : "") << "\n";
    }

    oss << "  ]\n";
    oss << "}\n";

    return oss.str();
}

// ********************************************************************************
// 險域ｸｬ繝倥Ν繝代・髢｢謨ｰ
// ********************************************************************************

/// @brief 鬮倡ｲｾ蠎ｦ繧ｿ繧､繝槭・・医・繧､繧ｯ繝ｭ遘貞腰菴搾ｼ・
class HighResolutionTimer {
public:
    using Clock = std::chrono::high_resolution_clock;
    using TimePoint = std::chrono::time_point<Clock>;
    using Duration = std::chrono::duration<double, std::micro>; // 繝槭う繧ｯ繝ｭ遘・

    void start() {
        startTime_ = Clock::now();
    }

    double elapsedMicroseconds() const {
        auto endTime = Clock::now();
        return std::chrono::duration_cast<Duration>(endTime - startTime_).count();
    }

    double elapsedMilliseconds() const {
        return elapsedMicroseconds() / 1000.0;
    }

private:
    TimePoint startTime_;
};

/// @brief 邨ｱ險域ュ蝣ｱ繧定ｨ育ｮ・
struct Statistics {
    double mean = 0.0;
    double min = 0.0;
    double max = 0.0;
    double stddev = 0.0;

    static Statistics compute(const std::vector<double>& values) {
        Statistics stats;
        if (values.empty()) {
            return stats;
        }

        // 蟷ｳ蝮・､
        double sum = 0.0;
        for (double v : values) {
            sum += v;
        }
        stats.mean = sum / values.size();

        // 譛蟆丞､繝ｻ譛螟ｧ蛟､
        stats.min = values[0];
        stats.max = values[0];
        for (double v : values) {
            if (v < stats.min) {
                stats.min = v;
            }
            if (v > stats.max) {
                stats.max = v;
            }
        }

        // 讓呎ｺ門￥蟾ｮ
        double variance = 0.0;
        for (double v : values) {
            double diff = v - stats.mean;
            variance += diff * diff;
        }
        variance /= values.size();
        stats.stddev = std::sqrt(variance);

        return stats;
    }
};

/// @brief 繝吶Φ繝√・繝ｼ繧ｯ邨先棡縺ｮ蜃ｺ蜉・
void printBenchmarkResult(const std::string& name, const Statistics& stats) {
    std::cout << " [" << name << "] ";
    std::cout << "Mean: " << std::fixed << std::setprecision(3) << stats.mean << " us, ";
    std::cout << "Min: " << stats.min << " us, ";
    std::cout << "Max: " << stats.max << " us, ";
    std::cout << "StdDev: " << stats.stddev << " us\n";
}

/// @brief 蜊倅ｸ繧ｹ繝ｬ繝・ラ縺ｧ繝輔ぃ繧､繝ｫ隱ｭ縺ｿ霎ｼ縺ｿ縺九ｉ繝代・繧ｹ縺ｾ縺ｧ繧定｡後≧縲・
/// @param filename 蜈･蜉帙ヵ繧｡繧､繝ｫ蜷阪・
/// @param out 隱ｭ縺ｿ霎ｼ縺ｿ邨先棡縺ｮ譬ｼ邏榊・縲・
/// @param fileReadTime 繝輔ぃ繧､繝ｫ隱ｭ縺ｿ霎ｼ縺ｿ譎る俣(繝槭う繧ｯ繝ｭ遘・縲・
/// @param parseTime 繝医・繧ｯ繝ｳ蛹匁凾髢・繝槭う繧ｯ繝ｭ遘・縲・
/// @param buildTime 繧ｪ繝悶ず繧ｧ繧ｯ繝域ｧ狗ｯ画凾髢・繝槭う繧ｯ繝ｭ遘・縲・
static void runSequentialPipeline(
    const std::string& filename,
    ComplexData& out,
    double& fileReadTime,
    double& parseTime,
    double& buildTime) {
    HighResolutionTimer timer;
    timer.start();
    std::ifstream ifs(filename, std::ios::binary);
    if (!ifs.is_open()) {
        throw std::runtime_error("runSequentialPipeline: failed to open file " + filename);
    }
    std::ostringstream oss;
    oss << ifs.rdbuf();
    std::string loadedData = oss.str();
    ifs.close();
    fileReadTime = timer.elapsedMicroseconds();

    timer.start();
    std::vector<char> buffer(loadedData.begin(), loadedData.end());
    constexpr std::size_t aheadSize = 8;
    buffer.reserve(buffer.size() + aheadSize);
    ReadingAheadBuffer inputSource(std::move(buffer), aheadSize);
    JsonTokenManager tokenManager;
    StdoutMessageOutput warningOutput;
    JsonTokenizer<ReadingAheadBuffer, JsonTokenManager> tokenizer(
        inputSource, tokenManager, warningOutput);
    tokenizer.tokenize();
    parseTime = timer.elapsedMicroseconds();

    timer.start();
    JsonParser<JsonTokenManager> parser(tokenManager);
    readJsonObject(parser, out);
    buildTime = timer.elapsedMicroseconds();
}

/// @brief 繧､繝ｳ繝｡繝｢繝ｪ繝吶Φ繝√・繝ｼ繧ｯ繧貞ｮ溯｡後☆繧九・
/// @param jsonData 繝・せ繝育畑JSON譁・ｭ怜・縲・
/// @param iterations 蜿榊ｾｩ蝗樊焚縲・
/// @param warmupCount 繧ｦ繧ｩ繝ｼ繝繧｢繝・・蝗樊焚縲・
static void runInMemoryBenchmark(const std::string& jsonData,
    int iterations, int warmupCount) {
    std::vector<double> readTimes, parseTimes, buildTimes, totalTimes;

    // 繧ｦ繧ｩ繝ｼ繝繧｢繝・・
    for (int i = 0; i < warmupCount; ++i) {
        ComplexData data;
        readJsonString(jsonData, data);
    }

    // 螳滓ｸｬ螳・
    for (int i = 0; i < iterations; ++i) {
        HighResolutionTimer timer;
        ComplexData data;

        // (1) 譁・ｭ怜・隱ｭ縺ｿ霎ｼ縺ｿ譎る俣
        timer.start();
        std::istringstream stream(jsonData);
        std::ostringstream oss;
        oss << stream.rdbuf();
        std::string loadedData = oss.str();
        double readTime = timer.elapsedMicroseconds();
        readTimes.push_back(readTime);

        // (2) 繝医・繧ｯ繝ｳ隗｣譫先凾髢・
        timer.start();
        std::vector<char> buffer(loadedData.begin(), loadedData.end());
        constexpr std::size_t aheadSize = 8;
        buffer.reserve(buffer.size() + aheadSize);
        ReadingAheadBuffer inputSource(std::move(buffer), aheadSize);
        JsonTokenManager tokenManager;
        StdoutMessageOutput warningOutput;
        JsonTokenizer<ReadingAheadBuffer, JsonTokenManager> tokenizer(
            inputSource, tokenManager, warningOutput);
        tokenizer.tokenize();
        double parseTime = timer.elapsedMicroseconds();
        parseTimes.push_back(parseTime);

        // (3) 繧ｪ繝悶ず繧ｧ繧ｯ繝域ｧ狗ｯ画凾髢・
        timer.start();
        JsonParser<JsonTokenManager> parser(tokenManager);
        readJsonObject(parser, data);
        double buildTime = timer.elapsedMicroseconds();
        buildTimes.push_back(buildTime);

        totalTimes.push_back(readTime + parseTime + buildTime);
    }

    // 邨ｱ險医ｒ險育ｮ励＠縺ｦ蜃ｺ蜉・
    std::cout << "Results:\n";
    printBenchmarkResult("(1) String Load    ", Statistics::compute(readTimes));
    printBenchmarkResult("(2) Token Parse    ", Statistics::compute(parseTimes));
    printBenchmarkResult("(3) Object Build   ", Statistics::compute(buildTimes));
    printBenchmarkResult("Total              ", Statistics::compute(totalTimes));
    std::cout << "\n";
}

/// @brief 繝輔ぃ繧､繝ｫI/O繝吶Φ繝√・繝ｼ繧ｯ繧貞ｮ溯｡後☆繧九・
/// @param jsonData 繝・せ繝育畑JSON譁・ｭ怜・縲・
/// @param filePrefix 繝輔ぃ繧､繝ｫ蜷阪・繝励Ξ繝輔ぅ繝・け繧ｹ縲・
/// @param iterations 蜿榊ｾｩ蝗樊焚縲・
/// @param warmupCount 繧ｦ繧ｩ繝ｼ繝繧｢繝・・蝗樊焚縲・
static void runFileIOBenchmark(
    const std::string& jsonData, const std::string& filePrefix,
    int iterations, int warmupCount) {
    std::vector<double> fileReadTimes, parseTimes, buildTimes, totalTimes;
    std::vector<double> sequentialFileInputTimes, parallelTotalTimes;

    // 蜈ｨ蜿榊ｾｩ蛻・・繝輔ぃ繧､繝ｫ繧剃ｺ句燕菴懈・
    std::vector<std::string> filenames;
    for (int i = 0; i < iterations; ++i) {
        std::string filename = filePrefix + std::to_string(i) + ".json";
        filenames.push_back(filename);
        std::ofstream ofs(filename, std::ios::out | std::ios::trunc);
        ofs << jsonData;
        ofs.close();
    }

    // 繧ｦ繧ｩ繝ｼ繝繧｢繝・・・域怙蛻昴・繝輔ぃ繧､繝ｫ縺ｮ縺ｿ・・
    for (int i = 0; i < warmupCount; ++i) {
        ComplexData data;
        double fr{}, pr{}, br{};
        runSequentialPipeline(filenames[0], data, fr, pr, br);
    }
    for (int i = 0; i < warmupCount; ++i) {
        ComplexData data;
        readJsonFile(filenames[0], data);
    }

    // 螳滓ｸｬ螳夲ｼ售equential Pipeline
    for (int i = 0; i < iterations; ++i) {
        ComplexData data;
        double fileReadTime{}, parseTime{}, buildTime{};
        runSequentialPipeline(filenames[i], data, fileReadTime, parseTime, buildTime);
        fileReadTimes.push_back(fileReadTime);
        parseTimes.push_back(parseTime);
        buildTimes.push_back(buildTime);
        totalTimes.push_back(fileReadTime + parseTime + buildTime);
    }

    // 螳滓ｸｬ螳夲ｼ售equential
    sequentialFileInputTimes.reserve(iterations);
    for (int i = 0; i < iterations; ++i) {
        ComplexData data;
        HighResolutionTimer timer;
        timer.start();
        readJsonFileSequential(filenames[i], data);
        sequentialFileInputTimes.push_back(timer.elapsedMicroseconds());
    }

    // 螳滓ｸｬ螳夲ｼ啀arallel
    parallelTotalTimes.reserve(iterations);
    for (int i = 0; i < iterations; ++i) {
        ComplexData data;
        HighResolutionTimer timer;
        timer.start();
        readJsonFileParallel(filenames[i], data);
        parallelTotalTimes.push_back(timer.elapsedMicroseconds());
    }

    // 螳滓ｸｬ螳夲ｼ哂uto (蛻・ｊ譖ｿ縺育沿)
    std::vector<double> autoTotalTimes;
    autoTotalTimes.reserve(iterations);
    for (int i = 0; i < iterations; ++i) {
        ComplexData data;
        HighResolutionTimer timer;
        timer.start();
        readJsonFile(filenames[i], data);
        autoTotalTimes.push_back(timer.elapsedMicroseconds());
    }

    // 繝・せ繝医ヵ繧｡繧､繝ｫ繧偵け繝ｪ繝ｼ繝ｳ繧｢繝・・
    for (const auto& filename : filenames) {
        std::remove(filename.c_str());
    }

    // 邨ｱ險医ｒ險育ｮ励＠縺ｦ蜃ｺ蜉・
    std::cout << "Sequential Results:\n";
    printBenchmarkResult("(1) File Read   ", Statistics::compute(fileReadTimes));
    printBenchmarkResult("(2) Token Parse ", Statistics::compute(parseTimes));
    printBenchmarkResult("(3) Object Build", Statistics::compute(buildTimes));
    printBenchmarkResult("Total           ", Statistics::compute(totalTimes));

    std::cout << "Sequential ReadingAheadBuffer (readJsonFileSequential):\n";
    printBenchmarkResult("Total              ", Statistics::compute(sequentialFileInputTimes));

    std::cout << "ParallelInputStreamSource (readJsonFileParallel):\n";
    printBenchmarkResult("Total              ", Statistics::compute(parallelTotalTimes));

    std::cout << "Auto Selection (readJsonFile):\n";
    printBenchmarkResult("Total              ", Statistics::compute(autoTotalTimes));
    std::cout << "\n";
}

// ********************************************************************************
// 繝吶Φ繝√・繝ｼ繧ｯ繝・せ繝・
// ********************************************************************************

/// @brief 蟆剰ｦ乗ｨ｡繝・・繧ｿ縺ｮ繝吶Φ繝√・繝ｼ繧ｯ
TEST(JsonBenchmark, SmallDataPerformance) {
    const int iterations = 100;
    std::string jsonData = generateSmallJsonData();
    std::cout << "\n=== Small Data Benchmark ===\n";
    std::cout << "Data size: " << jsonData.size() << " bytes, ";
    std::cout << "Iterations: " << iterations << "\n";
    runInMemoryBenchmark(jsonData, iterations, 10);
}

/// @brief 荳ｭ隕乗ｨ｡繝・・繧ｿ縺ｮ繝吶Φ繝√・繝ｼ繧ｯ
TEST(JsonBenchmark, MediumDataPerformance) {
    const int iterations = 50;
    std::string jsonData = generateMediumJsonData();
    std::cout << "\n=== Medium Data Benchmark ===\n";
    std::cout << "Data size: " << jsonData.size() << " bytes, ";
    std::cout << "Iterations: " << iterations << "\n";
    runInMemoryBenchmark(jsonData, iterations, 5);
}

/// @brief 繝輔ぃ繧､繝ｫI/O霎ｼ縺ｿ縺ｮ繝吶Φ繝√・繝ｼ繧ｯ・亥ｰ剰ｦ乗ｨ｡・・
/// @note 繧ｭ繝｣繝・す繝･縺ｮ蠖ｱ髻ｿ繧呈賜髯､縺吶ｋ縺溘ａ縲∝推蜿榊ｾｩ縺ｧ逡ｰ縺ｪ繧九ヵ繧｡繧､繝ｫ繧剃ｽｿ逕ｨ
TEST(JsonBenchmark, SmallDataFileIO) {
    const int iterations = 50;
    std::string jsonData = generateSmallJsonData();
    std::cout << "\n=== Small Data File I/O Benchmark ===\n";
    std::cout << "File size: " << jsonData.size() << " bytes, ";
    std::cout << "Iterations: " << iterations << "\n";
    // Note: Each iteration uses a different file to avoid cache effects
    runFileIOBenchmark(jsonData, "benchmark_small_", iterations, 3);
}

/// @brief 繝輔ぃ繧､繝ｫI/O霎ｼ縺ｿ縺ｮ繝吶Φ繝√・繝ｼ繧ｯ・井ｸｭ隕乗ｨ｡・・
/// @note 繧ｭ繝｣繝・す繝･縺ｮ蠖ｱ髻ｿ繧呈賜髯､縺吶ｋ縺溘ａ縲∝推蜿榊ｾｩ縺ｧ逡ｰ縺ｪ繧九ヵ繧｡繧､繝ｫ繧剃ｽｿ逕ｨ
TEST(JsonBenchmark, MediumDataFileIO) {
    const int iterations = 30;
    std::string jsonData = generateMediumJsonData();
    std::cout << "\n=== Medium Data File I/O Benchmark ===\n";
    std::cout << "File size: " << jsonData.size() << " bytes, ";
    std::cout << "Iterations: " << iterations << "\n";
    // Note: Each iteration uses a different file to avoid cache effects
    runFileIOBenchmark(jsonData, "benchmark_medium_", iterations, 2);
}
