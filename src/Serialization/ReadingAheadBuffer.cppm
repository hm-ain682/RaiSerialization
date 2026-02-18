// @file ReadingAheadBuffer.cppm
// @brief 先読み機能を持つバッファの実装。

module;
#include <string>
#include <cassert>

export module rai.serialization.reading_ahead_buffer;

export namespace rai::serialization {

/// @brief 先読みバッファ。
class ReadingAheadBuffer {
   public:
    using bufferType = std::string;

    /// @brief コンストラクタ。
    /// @param buffer バッファ本体。内容が入っていること。末尾における先読み分、capacityを確保しておくこと。
    /// @param aheadSize 先読みbyte数。
    explicit ReadingAheadBuffer(bufferType&& buffer, std::size_t aheadSize)
        : consumingBuffer_(std::move(buffer)), consumingValidSize_(consumingBuffer_.size()),
        consumingPos_(0), aheadSize_(aheadSize) {
        // 先読み用領域を'\0'で初期化しておく（番兵）。
        consumingBuffer_.resize(consumingValidSize_ + aheadSize_, '\0');
    }

    // コピー・ムーブ禁止
    ReadingAheadBuffer(const ReadingAheadBuffer&) = delete;
    ReadingAheadBuffer& operator=(const ReadingAheadBuffer&) = delete;
    ReadingAheadBuffer(ReadingAheadBuffer&&) = delete;
    ReadingAheadBuffer& operator=(ReadingAheadBuffer&&) = delete;

   public:
    /// @brief 現在の絶対読み取り位置を返す。
    /// @return 読み取り位置。
    std::size_t position() const { return consumingPos_; }

    /// @brief 先読みした文字を取得する。
    /// @param offset 現在位置からのオフセット。
    /// @return 指定位置の文字。範囲外の場合は'\0'。
    char peekAhead(std::size_t offset) const {
        assert(consumingPos_ + offset <= consumingBuffer_.size());
        return consumingBuffer_[consumingPos_ + offset];
    }

    /// @brief 現在位置から指定された文字数だけ読み進める。
    /// @param count 読み進める文字数。
    /// @note 文字を取得する場合は、事前にpeekAhead()を呼び出すこと。
    void consume(std::size_t count = 1) {
        assert(consumingPos_ + count <= consumingValidSize_);
        consumingPos_ += count;
    }

    //! トークン解析用の消費バッファ。末尾MaxOffset byteは先読み用。
    std::string consumingBuffer_;
    std::size_t consumingValidSize_; ///< 消費バッファの有効データ長。先読み用除く。
    std::size_t consumingPos_;       ///< 消費バッファ内の現在位置。
    std::size_t aheadSize_;          ///< 先読みbyte数。
};

}  // namespace rai::serialization
