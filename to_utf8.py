import os
import chardet

# 対象とする拡張子のセット（必要に応じて追加）
TARGET_EXTENSIONS = {".c", ".cpp", ".h", ".hpp", ".hlsl", ".hlsli", ".fx"}


def convert_to_utf8(file_path):
    # ファイルをバイナリモードで読み込み
    with open(file_path, "rb") as f:
        raw_data = f.read()

    # 既に UTF-8 BOM が付いている場合はスキップする
    if raw_data.startswith(b"\xef\xbb\xbf"):
        print(f"{file_path} は既に UTF-8 BOM 付きです。変換をスキップします。")
        return

    # エンコーディングを自動検出
    detected = chardet.detect(raw_data)
    encoding = detected["encoding"]
    print(
        f"{file_path} のエンコーディングは {encoding} と判定されました。UTF-8 BOM 付きに変換します。"
    )

    try:
        if encoding is None:
            print(
                f"{file_path} のエンコーディングが判定できなかったため、変換をスキップします。"
            )
            return

        # 変換対象のファイルを、検出されたエンコーディングで読み込む
        with open(file_path, "r", encoding=encoding, errors="replace") as f:
            content = f.read()

        # 'utf-8-sig' で書き出すと、BOM が自動的に付加される
        # with open(file_path, "w", encoding="utf-8-sig") as f:
        with open(file_path, "w", encoding="utf-8") as f:
            f.write(content)

        print(f"{file_path} を UTF-8 BOM 付きに変換しました。")
    except Exception as e:
        print(f"{file_path} の変換に失敗しました: {e}")


def convert_files_recursively(directory):
    """指定ディレクトリ以下を再帰的に探索し、対象拡張子のファイルを UTF-8 BOM 付きに変換する"""
    for root, dirs, files in os.walk(directory):
        for file_name in files:
            # ファイル拡張子の判定（小文字化して比較する）
            _, ext = os.path.splitext(file_name)
            if ext.lower() in TARGET_EXTENSIONS:
                file_path = os.path.join(root, file_name)
                convert_to_utf8(file_path)


if __name__ == "__main__":
    current_directory = os.getcwd()
    convert_files_recursively(current_directory)
