// @file JsonBenchmark.cpp
// @brief JSONパーサーのパフォーマンス計測

import rai.json.json_field;
import rai.json.json_writer;
import rai.json.json_parser;
import rai.json.json_tokenizer;
import rai.json.json_token_manager;
import rai.json.json_field_set;
import rai.json.json_io;
import rai.json.parallel_input_stream_source;
import rai.json.reading_ahead_buffer;
import rai.collection.sorted_hash_array_map;
#include <gtest/gtest.h>
#include <chrono>
#include <string>
#include <string_view>
#include <vector>
#include <memory>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <iostream>
#include <cmath>
#include <utility>

using namespace rai::json;

// ********************************************************************************
// ポリモーフィック型の登録（前方宣言）
// ********************************************************************************

struct BaseNode;
struct DataNode;
struct ContainerNode;

// ********************************************************************************
// テスト用データ構造
// ********************************************************************************

/// @brief プリミティブ型のみを持つ単純な構造体
struct SimpleData {
    int id = 0;
    double value = 0.0;
    bool flag = false;
    std::string name;

    const IJsonFieldSet& jsonFields() const {
        static const auto fields = makeJsonFieldSet<SimpleData>(
            getRequiredField(&SimpleData::id, "id"),
            getRequiredField(&SimpleData::value, "value"),
            getRequiredField(&SimpleData::flag, "flag"),
            getRequiredField(&SimpleData::name, "name")
        );
        return fields;
    }
};

/// @brief ベクターを含む構造体
struct VectorData {
    std::string category;
    std::vector<int> numbers;
    std::vector<std::string> tags;

    const IJsonFieldSet& jsonFields() const {
        static const auto fields = makeJsonFieldSet<VectorData>(
            getRequiredField(&VectorData::category, "category"),
            getRequiredField(&VectorData::numbers, "numbers"),
            getRequiredField(&VectorData::tags, "tags")
        );
        return fields;
    }
};

/// @brief ポリモーフィック型の基底クラス
struct BaseNode {
    std::string type;
    int nodeId = 0;

    virtual ~BaseNode() = default;

    virtual const IJsonFieldSet& jsonFields() const {
        static const auto fields = makeJsonFieldSet<BaseNode>(
            getRequiredField(&BaseNode::type, "type"),
            getRequiredField(&BaseNode::nodeId, "nodeId")
        );
        return fields;
    }
};

/// @brief ポリモーフィック型の派生クラス1
struct DataNode : public BaseNode {
    double dataValue = 0.0;

    const IJsonFieldSet& jsonFields() const override {
        static const auto fields = makeJsonFieldSet<DataNode>(
            getRequiredField(&BaseNode::type, "type"),
            getRequiredField(&BaseNode::nodeId, "nodeId"),
            getRequiredField(&DataNode::dataValue, "dataValue")
        );
        return fields;
    }
};

/// @brief ポリモーフィック型の派生クラス2
struct ContainerNode : public BaseNode {
    std::vector<std::string> children;

    const IJsonFieldSet& jsonFields() const override {
        static const auto fields = makeJsonFieldSet<ContainerNode>(
            getRequiredField(&BaseNode::type, "type"),
            getRequiredField(&BaseNode::nodeId, "nodeId"),
            getRequiredField(&ContainerNode::children, "children")
        );
        return fields;
    }
};

// ComplexData構造体は後で定義（ポリモーフィックエントリの後）

// ********************************************************************************
// ポリモーフィック型のディスパッチ登録
// ********************************************************************************

using MapEntry = std::pair<std::string_view, PolymorphicTypeFactory<std::unique_ptr<BaseNode>>>;

// ポリモーフィック型エントリマップ（makeSortedHashArrayMapを使用）
inline const auto baseNodeEntriesMap = rai::collection::makeSortedHashArrayMap(
    MapEntry{ std::string_view("DataNode"), [](){ return std::make_unique<DataNode>(); }},
    MapEntry{ std::string_view("ContainerNode"), [](){ return std::make_unique<ContainerNode>(); } }
);


// ComplexData構造体を再定義（ポリモーフィックフィールドを使用）
struct ComplexData {
    std::string name;
    int level = 0;
    std::unique_ptr<BaseNode> node;
    std::vector<SimpleData> items;
    std::vector<VectorData> collections;

    const IJsonFieldSet& jsonFields() const {
        static const auto fields = makeJsonFieldSet<ComplexData>(
            getRequiredField(&ComplexData::name, "name"),
            getRequiredField(&ComplexData::level, "level"),
            makeJsonPolymorphicField(&ComplexData::node, "node", baseNodeEntriesMap),
            getRequiredField(&ComplexData::items, "items"),
            getRequiredField(&ComplexData::collections, "collections")
        );
        return fields;
    }
};

// ********************************************************************************
// テストデータ生成
// ********************************************************************************

/// @brief 小規模データ生成（数KB程度）
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

    // 10個のアイテムを生成
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

    // 5個のコレクションを生成
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

/// @brief 中規模データ生成（数百KB程度）
std::string generateMediumJsonData() {
    std::ostringstream oss;
    oss << "{\n";
    oss << "  \"name\": \"MediumDataSet\",\n";
    oss << "  \"level\": 2,\n";
    oss << "  \"node\": {\n";
    oss << "    \"type\": \"ContainerNode\",\n";
    oss << "    \"nodeId\": 200,\n";
    oss << "    \"children\": [";

    // 100個の子ノード名を生成
    for (int i = 0; i < 100; ++i) {
        oss << "\"child_" << i << "\"" << (i < 99 ? ", " : "");
    }
    oss << "]\n";
    oss << "  },\n";
    oss << "  \"items\": [\n";

    // 1000個のアイテムを生成
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

    // 200個のコレクションを生成
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
// 計測ヘルパー関数
// ********************************************************************************

/// @brief 高精度タイマー（マイクロ秒単位）
class HighResolutionTimer {
public:
    using Clock = std::chrono::high_resolution_clock;
    using TimePoint = std::chrono::time_point<Clock>;
    using Duration = std::chrono::duration<double, std::micro>; // マイクロ秒

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

/// @brief 統計情報を計算
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

        // 平均値
        double sum = 0.0;
        for (double v : values) {
            sum += v;
        }
        stats.mean = sum / values.size();

        // 最小値・最大値
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

        // 標準偏差
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

/// @brief ベンチマーク結果の出力
void printBenchmarkResult(const std::string& name, const Statistics& stats) {
    std::cout << " [" << name << "] ";
    std::cout << "Mean: " << std::fixed << std::setprecision(3) << stats.mean << " us, ";
    std::cout << "Min: " << stats.min << " us, ";
    std::cout << "Max: " << stats.max << " us, ";
    std::cout << "StdDev: " << stats.stddev << " us\n";
}

/// @brief 単一スレッドでファイル読み込みからパースまでを行う。
/// @param filename 入力ファイル名。
/// @param out 読み込み結果の格納先。
/// @param fileReadTime ファイル読み込み時間(マイクロ秒)。
/// @param parseTime トークン化時間(マイクロ秒)。
/// @param buildTime オブジェクト構築時間(マイクロ秒)。
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
    JsonParser parser(tokenManager);
    readJsonObject(parser, out);
    buildTime = timer.elapsedMicroseconds();
}

/// @brief インメモリベンチマークを実行する。
/// @param jsonData テスト用JSON文字列。
/// @param iterations 反復回数。
/// @param warmupCount ウォームアップ回数。
static void runInMemoryBenchmark(const std::string& jsonData,
    int iterations, int warmupCount) {
    std::vector<double> readTimes, parseTimes, buildTimes, totalTimes;

    // ウォームアップ
    for (int i = 0; i < warmupCount; ++i) {
        ComplexData data;
        readJsonString(jsonData, data);
    }

    // 実測定
    for (int i = 0; i < iterations; ++i) {
        HighResolutionTimer timer;
        ComplexData data;

        // (1) 文字列読み込み時間
        timer.start();
        std::istringstream stream(jsonData);
        std::ostringstream oss;
        oss << stream.rdbuf();
        std::string loadedData = oss.str();
        double readTime = timer.elapsedMicroseconds();
        readTimes.push_back(readTime);

        // (2) トークン解析時間
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

        // (3) オブジェクト構築時間
        timer.start();
        JsonParser parser(tokenManager);
        readJsonObject(parser, data);
        double buildTime = timer.elapsedMicroseconds();
        buildTimes.push_back(buildTime);

        totalTimes.push_back(readTime + parseTime + buildTime);
    }

    // 統計を計算して出力
    std::cout << "Results:\n";
    printBenchmarkResult("(1) String Load    ", Statistics::compute(readTimes));
    printBenchmarkResult("(2) Token Parse    ", Statistics::compute(parseTimes));
    printBenchmarkResult("(3) Object Build   ", Statistics::compute(buildTimes));
    printBenchmarkResult("Total              ", Statistics::compute(totalTimes));
    std::cout << "\n";
}

/// @brief ファイルI/Oベンチマークを実行する。
/// @param jsonData テスト用JSON文字列。
/// @param filePrefix ファイル名のプレフィックス。
/// @param iterations 反復回数。
/// @param warmupCount ウォームアップ回数。
static void runFileIOBenchmark(
    const std::string& jsonData, const std::string& filePrefix,
    int iterations, int warmupCount) {
    std::vector<double> fileReadTimes, parseTimes, buildTimes, totalTimes;
    std::vector<double> sequentialFileInputTimes, parallelTotalTimes;

    // 全反復分のファイルを事前作成
    std::vector<std::string> filenames;
    for (int i = 0; i < iterations; ++i) {
        std::string filename = filePrefix + std::to_string(i) + ".json";
        filenames.push_back(filename);
        std::ofstream ofs(filename, std::ios::out | std::ios::trunc);
        ofs << jsonData;
        ofs.close();
    }

    // ウォームアップ（最初のファイルのみ）
    for (int i = 0; i < warmupCount; ++i) {
        ComplexData data;
        double fr{}, pr{}, br{};
        runSequentialPipeline(filenames[0], data, fr, pr, br);
    }
    for (int i = 0; i < warmupCount; ++i) {
        ComplexData data;
        readJsonFile(filenames[0], data);
    }

    // 実測定：Sequential Pipeline
    for (int i = 0; i < iterations; ++i) {
        ComplexData data;
        double fileReadTime{}, parseTime{}, buildTime{};
        runSequentialPipeline(filenames[i], data, fileReadTime, parseTime, buildTime);
        fileReadTimes.push_back(fileReadTime);
        parseTimes.push_back(parseTime);
        buildTimes.push_back(buildTime);
        totalTimes.push_back(fileReadTime + parseTime + buildTime);
    }

    // 実測定：Sequential
    sequentialFileInputTimes.reserve(iterations);
    for (int i = 0; i < iterations; ++i) {
        ComplexData data;
        HighResolutionTimer timer;
        timer.start();
        readJsonFileSequential(filenames[i], data);
        sequentialFileInputTimes.push_back(timer.elapsedMicroseconds());
    }

    // 実測定：Parallel
    parallelTotalTimes.reserve(iterations);
    for (int i = 0; i < iterations; ++i) {
        ComplexData data;
        HighResolutionTimer timer;
        timer.start();
        readJsonFileParallel(filenames[i], data);
        parallelTotalTimes.push_back(timer.elapsedMicroseconds());
    }

    // 実測定：Auto (切り替え版)
    std::vector<double> autoTotalTimes;
    autoTotalTimes.reserve(iterations);
    for (int i = 0; i < iterations; ++i) {
        ComplexData data;
        HighResolutionTimer timer;
        timer.start();
        readJsonFile(filenames[i], data);
        autoTotalTimes.push_back(timer.elapsedMicroseconds());
    }

    // テストファイルをクリーンアップ
    for (const auto& filename : filenames) {
        std::remove(filename.c_str());
    }

    // 統計を計算して出力
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
// ベンチマークテスト
// ********************************************************************************

/// @brief 小規模データのベンチマーク
TEST(JsonBenchmark, SmallDataPerformance) {
    const int iterations = 100;
    std::string jsonData = generateSmallJsonData();
    std::cout << "\n=== Small Data Benchmark ===\n";
    std::cout << "Data size: " << jsonData.size() << " bytes, ";
    std::cout << "Iterations: " << iterations << "\n";
    runInMemoryBenchmark(jsonData, iterations, 10);
}

/// @brief 中規模データのベンチマーク
TEST(JsonBenchmark, MediumDataPerformance) {
    const int iterations = 50;
    std::string jsonData = generateMediumJsonData();
    std::cout << "\n=== Medium Data Benchmark ===\n";
    std::cout << "Data size: " << jsonData.size() << " bytes, ";
    std::cout << "Iterations: " << iterations << "\n";
    runInMemoryBenchmark(jsonData, iterations, 5);
}

/// @brief ファイルI/O込みのベンチマーク（小規模）
/// @note キャッシュの影響を排除するため、各反復で異なるファイルを使用
TEST(JsonBenchmark, SmallDataFileIO) {
    const int iterations = 50;
    std::string jsonData = generateSmallJsonData();
    std::cout << "\n=== Small Data File I/O Benchmark ===\n";
    std::cout << "File size: " << jsonData.size() << " bytes, ";
    std::cout << "Iterations: " << iterations << "\n";
    // 注: 各イテレーションでは異なるファイルを使用してキャッシュの影響を避けます
    runFileIOBenchmark(jsonData, "benchmark_small_", iterations, 3);
}

/// @brief ファイルI/O込みのベンチマーク（中規模）
/// @note キャッシュの影響を排除するため、各反復で異なるファイルを使用
TEST(JsonBenchmark, MediumDataFileIO) {
    const int iterations = 30;
    std::string jsonData = generateMediumJsonData();
    std::cout << "\n=== Medium Data File I/O Benchmark ===\n";
    std::cout << "File size: " << jsonData.size() << " bytes, ";
    std::cout << "Iterations: " << iterations << "\n";
    // 注: 各イテレーションでは異なるファイルを使用してキャッシュの影響を避けます
    runFileIOBenchmark(jsonData, "benchmark_medium_", iterations, 2);
}
