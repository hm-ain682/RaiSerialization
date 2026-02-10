// @file ReadingAheadDoubleBuffer.cppm
// @brief 二重バッファで先読み機能を提供するクラス。

module;
#include <algorithm>
#include <cassert>
#include <stdexcept>
#include <vector>

export module rai.serialization.reading_ahead_double_buffer;

export namespace rai::serialization {

/// @brief 二重バッファで先読み機能を提供するクラス。
/// @note 読み込み用と消費用の二重バッファで効率的にデータを処理する。
/// @tparam T バッファの要素型。
template <typename T>
class ReadingAheadDoubleBuffer {
public:
    /// @brief コンストラクタ。
    /// @param maxReadingAhead 先読みサイズ(要素数)。
    /// @param bufferCapacity バッファ容量。ファイル読み込み効率のためページサイズの倍数を推奨。
    explicit ReadingAheadDoubleBuffer(std::size_t maxReadingAhead = 8,
                                      std::size_t bufferCapacity = 4096)
        : maxReadingAhead_(maxReadingAhead),
          consumingValidSize_(0),
          consumingPos_(0),
          absolutePos_(0),
          readingValidSize_(0) {
        if (bufferCapacity <= maxReadingAhead_) {
            throw std::invalid_argument(
                "bufferCapacity must be at least maxReadingAhead + 1");
        }
        consumingBuffer_.resize(bufferCapacity);
    }

    // コピー・ムーブ禁止
    ReadingAheadDoubleBuffer(const ReadingAheadDoubleBuffer&) = delete;
    ReadingAheadDoubleBuffer& operator=(const ReadingAheadDoubleBuffer&) = delete;
    ReadingAheadDoubleBuffer(ReadingAheadDoubleBuffer&&) = delete;
    ReadingAheadDoubleBuffer& operator=(ReadingAheadDoubleBuffer&&) = delete;

    /// @brief 先読みサイズを取得する。
    /// @return 先読みサイズ（要素数）。
    std::size_t maxReadingAhead() const {
        return maxReadingAhead_;
    }

    /// @brief 最大連続消費可能サイズを取得する。
    /// @return 最大連続消費可能サイズ(要素数)。
    std::size_t maxConsumeSize() const {
        return consumingBuffer_.size() - maxReadingAhead_;
    }

    /// @brief 現在の絶対読み取り位置を返す。
    /// @return 読み取り位置。
    std::size_t position() const {
        // absolutePos_はメインスレッドでしか扱わないためロック不要。
        return absolutePos_;
    }

    /// @brief 消費バッファの容量を取得する。
    /// @return 消費バッファの容量。
    std::size_t consumingCapacity() const {
        return consumingBuffer_.size();
    }

    /// @brief 消費バッファの容量を設定する。
    /// @param capacity 新しい消費バッファの容量。
    void resizeConsumingBuffer(std::size_t capacity) {
        consumingBuffer_.resize(capacity);
    }

    /// @brief 消費バッファのデータポインタを取得する。
    /// @return 消費バッファのデータポインタ。
    T* consumingData() {
        return consumingBuffer_.data();
    }

    /// @brief 消費有効サイズを取得する。
    /// @return 消費有効サイズ。
    std::size_t consumingValidSize() const {
        return consumingValidSize_;
    }

    /// @brief 消費有効サイズを設定する。
    /// @param size 新しい消費有効サイズ。
    void setConsumingValidSize(std::size_t size) {
        consumingValidSize_ = size;
    }

    /// @brief 消費位置を取得する。
    /// @return 消費位置。
    std::size_t consumingPos() const {
        return consumingPos_;
    }

    /// @brief 消費位置を設定する。
    /// @param pos 新しい消費位置。
    void setConsumingPos(std::size_t pos) {
        consumingPos_ = pos;
    }

    /// @brief 消費バッファが空かどうかを判定する。
    /// @return 消費バッファが空の場合true。
    bool isConsumingBufferEmpty() const {
        return consumingPos_ >= consumingValidSize_;
    }

    /// @brief 読み込みバッファのデータポインタを取得する。
    /// @return 読み込みバッファのデータポインタ。
    T* readingData() {
        return readingBuffer_.data();
    }

    /// @brief 読み込みバッファをリサイズする。
    /// @param size 新しいサイズ。
    void resizeReadingBuffer(std::size_t size) {
        readingBuffer_.resize(size);
    }

    /// @brief 読み込み有効サイズを取得する。
    /// @return 読み込み有効サイズ。
    std::size_t readingValidSize() const {
        return readingValidSize_;
    }

    /// @brief 読み込み有効サイズを設定する。
    /// @param size 新しい読み込み有効サイズ。
    void setReadingValidSize(std::size_t size) {
        readingValidSize_ = size;
    }

    /// @brief 読み込みバッファがいっぱいかどうかを判定する。
    /// @return 読み込みバッファがいっぱいの場合true。
    bool isReadingBufferFull() const {
        return readingValidSize_ >= readingBuffer_.size();
    }

    /// @brief 初回読み込み後の初期化を行う。
    /// @param bytesRead 初回読み込みで読み取ったバイト数。
    void initializeFromFirstRead(std::size_t bytesRead) {
        consumingPos_ = 0;
        auto maxConsume = maxConsumeSize();

        if (bytesRead <= maxConsume) {
            // ファイルサイズが十分小さいので、読み込みバッファ不要。
            // 先読み用にmaxReadingAhead_要素分デフォルト値で埋める。
            std::fill_n(consumingBuffer_.data() + bytesRead, maxReadingAhead_, T{});
            consumingValidSize_ = bytesRead;
        }
        else {
            // 次回ファイル読み込みと先読みの残り分のため、読み込みバッファを用意する。
            resizeReadingBuffer(consumingBuffer_.size());
            // 消費バッファの[maxConsume, bytesRead)を読み込みバッファの先頭にコピー。
            auto copySize = bytesRead - maxConsume;
            std::copy_n(consumingBuffer_.data() + maxConsume, copySize, readingBuffer_.data());
            // 消費バッファの有効サイズを設定（copyした分は除く）。
            consumingValidSize_ = maxConsume;
            // readingValidSize_は0のまま（バックグラウンドスレッドが設定する）。
            readingValidSize_ = 0;
        }
    }

    /// @brief 読み込みバッファに要素を追加する。読み込みバッファに空きがあることをあらかじめ確認しておくこと。
    /// @param element 追加する要素。
    void push(T&& element) {
        assert(readingValidSize_ < readingBuffer_.size());
        // 書き込み先は読み込みバッファの末尾
        readingBuffer_[readingValidSize_] = std::move(element);
        readingValidSize_++;
    }

    /// @brief 現在位置の要素を取得し、消費位置を1つ進める。
    /// @return 現在位置の要素への参照。
    T& take() {
        auto& element = consumingBuffer_[consumingPos_];
        advance(1);
        return element;
    }

    /// @brief 先読みした要素を取得する。
    /// @param offset 現在位置からのオフセット。
    /// @return 指定位置の要素への参照。範囲外アクセスは未定義動作。
    const T& peekAhead(std::size_t offset) const {
        // ファイル読み込み中にconsumingBuffer_を参照することはないためロック不要。
        return consumingBuffer_[consumingPos_ + offset];
    }

    /// @brief 消費位置と絶対位置を同時に進める。
    /// @param count 進める文字数。
    void advance(std::size_t count) {
        consumingPos_ += count;
        absolutePos_ += count;
    }

    /// @brief 消費位置を消費有効サイズに設定し、その差分だけ絶対位置を進める。
    void moveToValidEnd() {
        auto d = consumingValidSize_ - consumingPos_;
        consumingPos_ = consumingValidSize_;
        absolutePos_ += d;
    }

    /// @brief EOF到達時の先読み用バッファ準備処理。
    /// @param count 追加で進める文字数。
    void prepareEofBuffer(std::size_t count) {
        auto dataEnd = consumingValidSize_ + maxReadingAhead_;  // maxReadingAhead_は未取得分。
        auto first = consumingBuffer_.begin();
        if (dataEnd + maxReadingAhead_ > consumingBuffer_.size()) {
            // 最終要素到達時の先読み分を入れる余地がない。
            // 次に取得する位置を先頭に移動する。
            std::shift_left(first, first + dataEnd, consumingValidSize_);
            dataEnd = maxReadingAhead_;
        }
        std::fill_n(first + dataEnd, maxReadingAhead_, T{});
        consumingValidSize_ = dataEnd;
        consumingPos_ += count;
        absolutePos_ += count;
    }

    /// @brief バッファを入れ替えて、消費位置をリセットする。
    void swapAndReset() {
        // バッファを入れ替える。
        consumingBuffer_.swap(readingBuffer_);

        // swap後に新しい消費バッファの有効サイズを計算し、
        // 新しい読み込みバッファの先頭に新しい消費バッファの末尾をコピー。
        auto contentEnd = readingValidSize_ + maxReadingAhead_;
        auto maxConsume = maxConsumeSize();

        if (contentEnd > maxConsume) {
            // データが十分にある場合、新しい消費バッファの末尾を新しい読み込みバッファにコピー。
            // どうしてこの実装にしたか：次のバッファに切り替えた時に、
            // 前のバッファの末尾maxReadingAhead_要素を先読みできるようにするため。
            auto copySize = contentEnd - maxConsume;
            std::copy_n(consumingBuffer_.data() + maxConsume,
                        copySize,
                        readingBuffer_.data());
            consumingValidSize_ = contentEnd - copySize;  // = maxConsume
        } else {
            // EOFに近く、データが少ない場合はコピー不要。
            consumingValidSize_ = contentEnd - maxReadingAhead_;  // = readingValidSize_
        }

        consumingPos_ = 0;
        readingValidSize_ = 0;
    }

private:
    std::size_t maxReadingAhead_;     ///< 先読みサイズ(要素数)。
    //! トークン解析用の消費バッファ。末尾maxReadingAhead_要素は先読み用。
    //! 消費がそこに到達したらバッファを切り替える。
    std::vector<T> consumingBuffer_;
    std::size_t consumingValidSize_;  ///< 消費バッファの有効データ長。先読み用除く。
    std::size_t consumingPos_;        ///< 消費バッファ内の現在位置。
    std::size_t absolutePos_;         ///< 入力全体での読み取り位置。
    //! ファイルから読み込み中のバッファ。先頭maxReadingAhead_要素は消費バッファ末尾と同じ。
    std::vector<T> readingBuffer_;
    std::size_t readingValidSize_;  ///< 読み込みバッファの有効データ長。
};

}  // namespace rai::serialization
