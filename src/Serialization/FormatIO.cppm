/// @file FormatIO.cppm
/// @brief 既定フォーマットのReader/Writer型を束ねる。

module;

export module rai.serialization.format_io;

import rai.serialization.json_writer;
import rai.serialization.parser;
import rai.serialization.token_manager;

export namespace rai::serialization {

/// @brief 既定フォーマットの書き込み型。
using FormatWriter = JsonWriter;

/// @brief 既定フォーマットの読み込み型。
using FormatReader = Parser;

/// @brief 既定フォーマットのトークン種別。
using FormatTokenType = JsonTokenType;

}  // namespace rai::serialization
