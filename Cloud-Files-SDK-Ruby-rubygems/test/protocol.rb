require 'cloud_files_sdk'
client=CloudFilesSDKSDK::Client.new(library_path:ARGV[0]);begin
 begin;client.call('check');raise 'Invalid request accepted';rescue CloudFilesSDKSDK::NativeError;end
 tests=JSON.parse("[{\"operation\": \"check\", \"arguments\": {\"identity\": \"synthetic-catalog\", \"entries\": []}, \"expected_key\": \"valid\", \"expected_value\": true}]")
 tests.each do |test|
 result=client.call(test['operation'],test['arguments']);value=result[test['expected_key']];raise 'Contract mismatch' if test.key?('expected_value') && value!=test['expected_value'];raise 'Array missing' if test.key?('minimum_length') && value.size<test['minimum_length'];end if ARGV[0]
ensure;client.close;end
