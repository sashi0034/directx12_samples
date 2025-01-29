import os
import chardet

# 対象とする拡張子のセット（必要に応じて追加）
TARGET_EXTENSIONS = {'.c', '.cpp', '.h', '.hpp', '.hlsl', '.hlsli'}


def convert_to_utf8(file_path):
    with open(file_path, 'rb') as f:
        raw_data = f.read()

    # エンコーディングを自動検出
    detected = chardet.detect(raw_data)
    encoding = detected['encoding']
    print(f"Converting {file_path} from {encoding} to UTF-8")

    # ファイルをUTF-8に変換
    try:
        if encoding and encoding.lower() != 'utf-8':
            with open(file_path, 'r', encoding=encoding) as f:
                content = f.read()
            with open(file_path, 'w', encoding='utf-8') as f:
                f.write(content)
            print(f"Converted {file_path} to UTF-8 successfully.")
        else:
            print(f"{file_path} is already in UTF-8 or encoding could not be detected.")
    except Exception as e:
        print(f"Failed to convert {file_path}: {e}")


def convert_files_recursively(directory):
    """directory以下を再帰的に探索し、対象拡張子のファイルをUTF-8へ変換する"""
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
