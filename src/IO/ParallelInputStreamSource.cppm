// @file ParallelInputStreamSource.cppm
// @brief ファイル入力を二重バッファで扱う入力ソース。

module;
#include <algorithm>
#include <array>
#include <condition_variable>
#include <istream>
#include <mutex>
#include <future>
#include <vector>
#include <cassert>

export module rai.json.parallel_input_stream_source;
import rai.json.reading_ahead_double_buffer;
import rai.json.reading_ahead_buffer;
import rai.common.thread_pool;

export namespace rai::json {

/// @brief ファイルストリームから並列安全にデータを読み取る入力ソース。
/// @note 二重バッファを用いて効率的にファイル読み込みを行う。
/// @note バックグラウンドスレッドでファイル読み込みを並列実行する。
class ParallelInputStreamSource {
public:
    /// @brief 入力ソースを構築する。
    /// @param stream 入力ストリーム。
    explicit ParallelInputStreamSource(std::istream& stream)
        : stream_(stream),
          eof_(false),
          readingInProgress_(false),
          threadPool_(rai::common::getGlobalThreadPool()) {
        // 消費バッファを事前確保し、初回読み込みを同期的に実行して処理を開始可能にする。
        stream_.read(buffer_.consumingData(),
            static_cast<std::streamsize>(buffer_.consumingCapacity()));
        std::size_t bytesRead = static_cast<std::size_t>(stream_.gcount());
        eof_ = isEof(bytesRead);

        buffer_.initializeFromFirstRead(bytesRead);
        if (!eof_) {
            std::unique_lock<std::mutex> lock(mutex_);
            requestReadIfNeeded(lock);
        }
    }

    /// @brief デストラクタ。バックグラウンドスレッドを終了する。
    ~ParallelInputStreamSource() {
        condition_.notify_all();
        waitForPendingTask();
        {
            std::unique_lock<std::mutex> lock(mutex_);
            // predicate版でスプリアスウェイクアップを扱い、条件の評価をまとめる
            condition_.wait(lock, [this]() { return !readingInProgress_; });
        }
    }

    // コピー・ムーブ禁止（スレッド管理のため）
    ParallelInputStreamSource(const ParallelInputStreamSource&) = delete;
    ParallelInputStreamSource& operator=(const ParallelInputStreamSource&) = delete;
    ParallelInputStreamSource(ParallelInputStreamSource&&) = delete;
    ParallelInputStreamSource& operator=(ParallelInputStreamSource&&) = delete;

    /// @brief 現在の絶対読み取り位置を返す。
    /// @return 読み取り位置。
    std::size_t position() const {
        return buffer_.position();
    }

    /// @brief 先読みした文字を取得する。
    /// @param offset 現在位置からのオフセット。
    /// @return 指定位置の文字。範囲外の場合は'\0'。
    char peekAhead(std::size_t offset) const {
        return buffer_.peekAhead(offset);
    }

    /// @brief 現在位置から指定された文字数だけ読み進める。
    /// @param count 読み進める文字数。
    /// @note 文字を取得する場合は、事前にpeekAhead()を呼び出すこと。
    void consume(std::size_t count = 1) {
        assert(buffer_.consumingPos() <= buffer_.consumingValidSize());
        if (buffer_.consumingPos() + count <= buffer_.consumingValidSize()) {
            // 進めた後もバッファ内。
            buffer_.advance(count);
            return;
        }

        // 消費バッファ有効末尾まで進める。
        auto d = buffer_.consumingValidSize() - buffer_.consumingPos();
        count -= d;
        buffer_.moveToValidEnd();

        if (swapBuffers()) {
            // バッファ入れ替えした。
            if (count <= buffer_.consumingValidSize()) {
                // バッファ入れ替え後にバッファ内。
                buffer_.advance(count);
            } else {  // ファイル末尾で残った分。
                buffer_.moveToValidEnd();
            }
        }
        else {
            // EOF到達。
            buffer_.prepareEofBuffer(count);
        }
    }

    /// @brief 消費バッファと読み込みバッファを入れ替える。
    /// @return 入れ替えればtrue。
    bool swapBuffers() {
        std::unique_lock<std::mutex> lock(mutex_);

        // 読み込みバッファが空でかつEOF未到達のときのみ待機し、
        // スレッドプールで非同期読み込みが完了するのを待つ
        condition_.wait(lock, [this]() {
            return !(((buffer_.readingValidSize() == 0) && !eof_) || readingInProgress_);
        });
        if (buffer_.readingValidSize() == 0) {
            return false; // なにも読み込めなかった。EOF到達後。
        }

        buffer_.swapAndReset();
        if (!eof_) {
            requestReadIfNeeded(lock);
        }
        return true;
    }

    /// @brief スレッドプールへ非同期読み込みを要求する。
    /// @param lock 呼び出し元で取得済みのロック。
    void requestReadIfNeeded(std::unique_lock<std::mutex>& lock) {
        assert(buffer_.readingValidSize() == 0);
        lock.unlock();
        std::future<void> taskFuture = threadPool_.enqueue([this]() {
            readNextChunkTask();
        });
        lock.lock();
        pendingReadTask_ = std::move(taskFuture);
    }

    /// @brief スレッドプール上で1回分の読み込み処理を行う。
    void readNextChunkTask() {
        std::unique_lock<std::mutex> lock(mutex_);
        readingInProgress_ = true;
        lock.unlock();

        // 読み込みバッファの先頭は前の消費バッファの末尾の先読み分。
        stream_.read(buffer_.readingData() + buffer_.maxReadingAhead(),
            static_cast<std::streamsize>(buffer_.maxConsumeSize()));
        std::size_t bytesRead = static_cast<std::size_t>(stream_.gcount());
        bool eof = isEof(bytesRead);

        lock.lock();
        buffer_.setReadingValidSize(bytesRead);
        eof_ = eof;
        readingInProgress_ = false;
        condition_.notify_all();
    }

    /// @brief 保留中の読み込みタスク完了を待機する。
    void waitForPendingTask() {
        std::future<void> task;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (!pendingReadTask_.valid()) {
                return;
            }
            task = std::move(pendingReadTask_);
        }
        task.wait();
    }

    /// @brief EOFに到達していればtrueを返す。
    /// @param bytesRead 直前のreadで読み取れたバイト数。
    bool isEof(std::size_t bytesRead) {
        // 読み取れたサイズが指定サイズ未満ならEOF到達。eof()はエラーの場合。
        return bytesRead < buffer_.maxConsumeSize() || stream_.eof();
    }

    ReadingAheadDoubleBuffer<char> buffer_;  ///< 二重バッファ。
    std::istream& stream_;  ///< 入力ストリーム。
    bool eof_;  ///< EOF到達フラグ。

    // スレッド制御用メンバー
    bool readingInProgress_;  ///< 読み込み実行中フラグ。
    mutable std::mutex mutex_;  ///< 並列アクセス保護用ミューテックス。
    mutable std::condition_variable condition_;  ///< スレッド間同期用条件変数。
    rai::common::ThreadPool& threadPool_;  ///< 共有ThreadPoolへの参照。
    std::future<void> pendingReadTask_;  ///< 実行中または待機中の読み込みタスク。
};

}  // namespace rai::json
