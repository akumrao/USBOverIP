mkdir -p third_party
# Download the official zip archive directly from Google's delivery storage network
curl -L https://dl.google.com/firebase/sdk/cpp/firebase_cpp_sdk_13.12.0.zip -o firebase_sdk.zip
# Unzip it directly into the target folder structure
unzip firebase_sdk.zip -d third_party/firebase_cpp_sdk
# Clean up the downloaded archive file
rm firebase_sdk.zip
