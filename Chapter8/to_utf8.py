import os
import chardet


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


# 現在のディレクトリ内のファイルを変換
def convert_files_in_current_directory():
    current_directory = os.getcwd()  # 現在のディレクトリ
    for file_name in os.listdir(current_directory):
        file_path = os.path.join(current_directory, file_name)
        if os.path.isfile(file_path):  # ファイルのみを対象
            convert_to_utf8(file_path)


if __name__ == "__main__":
    convert_files_in_current_directory()
