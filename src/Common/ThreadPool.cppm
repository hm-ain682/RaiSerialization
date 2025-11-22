// @file ThreadPool.cppm
// @brief シンプルなスレッドプール実装。タスクキューを管理し、並列実行を提供する。

module;
#include <vector>
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <future>
#include <type_traits>

export module rai.compiler.common.thread_pool;

namespace rai::common {

/// @brief シンプルなスレッドプール。タスクを並列実行する。
export class ThreadPool {
public:
    /// @brief コンストラクタ。指定された数のワーカースレッドを起動する。
    /// @param numThreads ワーカースレッドの数。0の場合はハードウェアの並列度を使用する。
    explicit ThreadPool(size_t numThreads = 0)
        : stop_(false), activeTasks_(0) {
        // どうしてこの実装にしたか：numThreadsが0の場合、ハードウェアの並列度に基づいて適切なスレッド数を自動設定する
        if (numThreads == 0) {
            numThreads = std::thread::hardware_concurrency();
            if (numThreads == 0) {
                numThreads = 2;  // フォールバック値
            }
        }

        // ワーカースレッドを起動
        workers_.reserve(numThreads);
        for (size_t i = 0; i < numThreads; ++i) {
            workers_.emplace_back([this] { workerThread(); });
        }
    }

    /// @brief デストラクタ。全てのタスクを完了してからスレッドを終了する。
    ~ThreadPool() {
        {
            std::unique_lock<std::mutex> lock(queueMutex_);
            stop_ = true;
        }
        condition_.notify_all();

        // どうしてこの実装にしたか：全てのワーカースレッドが確実に終了するまで待機する
        for (std::thread& worker : workers_) {
            if (worker.joinable()) {
                worker.join();
            }
        }
    }

    // コピー・ムーブ禁止
    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;
    ThreadPool(ThreadPool&&) = delete;
    ThreadPool& operator=(ThreadPool&&) = delete;

    /// @brief タスクをキューに追加し、実行結果を受け取るfutureを返す。
    /// @tparam F 実行するコール可能オブジェクトの型。
    /// @param task 実行するタスク。
    /// @return タスク完了を待機するfuture。
    template <class F>
    auto enqueue(F&& task) -> std::future<std::invoke_result_t<F>> {
        using ReturnType = std::invoke_result_t<F>;

        auto packagedTask =
            std::make_shared<std::packaged_task<ReturnType()>>(std::forward<F>(task));
        std::future<ReturnType> future = packagedTask->get_future();

        {
            std::unique_lock<std::mutex> lock(queueMutex_);
            if (stop_) {
                throw std::runtime_error("ThreadPool: Cannot enqueue task after stop");
            }
            tasks_.push([packagedTask]() {
                (*packagedTask)();
            });
        }
        condition_.notify_one();
        return future;
    }

    /// @brief 全てのタスクが完了するまで待機する。
    void waitForCompletion() {
        std::unique_lock<std::mutex> lock(queueMutex_);
        completionCondition_.wait(lock, [this] {
            return tasks_.empty() && activeTasks_ == 0;
        });
    }

    /// @brief ワーカースレッドの数を取得する。
    /// @return ワーカースレッドの数。
    size_t getThreadCount() const {
        return workers_.size();
    }

private:
    /// @brief ワーカースレッドの実行関数。タスクキューからタスクを取得して実行する。
    void workerThread() {
        while (true) {
            std::function<void()> task;

            {
                std::unique_lock<std::mutex> lock(queueMutex_);
                // 停止フラグが立つか、タスクが追加されるまで待機する
                condition_.wait(lock, [this] {
                    return stop_ || !tasks_.empty();
                });
                if (stop_) {
                    return; // 停止要求による停止。
                }

                // waitの述語により、ここではtasks_が空でないことが保証されている
                task = std::move(tasks_.front());
                tasks_.pop();
                ++activeTasks_;
            }

            // ロックを解放してからタスクを実行することで並列度を高める
            task();

            {
                std::unique_lock<std::mutex> lock(queueMutex_);
                --activeTasks_;
                // 条件分岐の評価はnotify_all()よりも高速なため、不要な通知を避けることで効率化できる
                if (tasks_.empty() && activeTasks_ == 0) { // // 全タスクが完了した。
                    completionCondition_.notify_all();
                }
            }
        }
    }

    std::vector<std::thread> workers_;            ///< ワーカースレッドのコレクション。
    std::queue<std::function<void()>> tasks_;     ///< タスクキュー。
    std::mutex queueMutex_;                       ///< タスクキューへのアクセスを保護するミューテックス。
    std::condition_variable condition_;           ///< タスクの追加や停止を通知する条件変数。
    std::condition_variable completionCondition_; ///< タスクの完了を通知する条件変数。
    bool stop_;                                   ///< 停止フラグ。trueの場合、新しいタスクを受け付けない。
    size_t activeTasks_;                          ///< 現在実行中のタスクの数。queueMutex_で保護される。
};

/// @brief 共有のThreadPoolインスタンスを取得する。
/// @return グローバルThreadPoolインスタンス。
export ThreadPool& getGlobalThreadPool() {
    static ThreadPool globalPool;
    return globalPool;
}

}  // namespace rai::common
