#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <openssl/sha.h>
#include <openssl/err.h>
#include <openssl/hmac.h>
#include <iomanip>
#include <sstream>
#include <ctime>

using namespace std;

#define AES_256_KEY_SIZE 32
#define AES_GCM_IV_SIZE 12
#define AES_GCM_TAG_SIZE 16
#define AES_BLOCK_SIZE 16
#define SALT_SIZE 32
#define PBKDF2_ITERATIONS 310000

string bytes_to_hex(const vector<unsigned char>& bytes) {
    stringstream ss;
    ss << hex << setfill('0');
    for (size_t i = 0; i < bytes.size(); ++i) {
        ss << setw(2) << (int)bytes[i];
    }
    return ss.str();
}

vector<unsigned char> hex_to_bytes(const string& hex_str) {
    vector<unsigned char> bytes;
    for (size_t i = 0; i < hex_str.length(); i += 2) {
        string byteString = hex_str.substr(i, 2);
        unsigned char byte = (unsigned char) strtol(byteString.c_str(), NULL, 16);
        bytes.push_back(byte);
    }
    return bytes;
}

string base64url_encode(const string& in) {
    int len = ((in.length() + 2) / 3) * 4;
    vector<unsigned char> out(len + 1);
    int written = EVP_EncodeBlock(out.data(), (const unsigned char*)in.data(), in.length());
    string b64((char*)out.data(), written);
    
    for(auto& c : b64) {
        if(c == '+') c = '-';
        else if(c == '/') c = '_';
    }
    while(!b64.empty() && b64.back() == '=') b64.pop_back();
    return b64;
}

string base64url_decode(const string& in) {
    string b64 = in;
    for(auto& c : b64) {
        if(c == '-') c = '+';
        else if(c == '_') c = '/';
    }
    while(b64.length() % 4 != 0) b64 += "=";
    
    int len = (b64.length() * 3) / 4;
    vector<unsigned char> out(len + 1);
    
    int padding = 0;
    if (b64.length() >= 2) {
        if(b64[b64.length()-1] == '=') padding++;
        if(b64[b64.length()-2] == '=') padding++;
    }
    
    int written = EVP_DecodeBlock(out.data(), (const unsigned char*)b64.data(), b64.length());
    if (written < 0) return "";
    return string((char*)out.data(), written - padding);
}

string hmac_sha256(const string& msg, const string& key) {
    unsigned char hash[EVP_MAX_MD_SIZE];
    unsigned int hash_len;
    HMAC(EVP_sha256(), key.data(), key.length(), (const unsigned char*)msg.data(), msg.length(), hash, &hash_len);
    return string((char*)hash, hash_len); 
}

string str_extract(const string& json, const string& key, const string& term) {
    size_t start = json.find(key);
    if (start == string::npos) return "";
    start += key.length();
    size_t end = json.find(term, start);
    if (end == string::npos) return json.substr(start);
    return json.substr(start, end - start);
}

void print_json(const string& salt, const string& file_hash, const string& status, const string& error_msg = "") {
    cout << "{\n";
    cout << "  \"salt\": \"" << salt << "\",\n";
    cout << "  \"hash\": \"" << file_hash << "\",\n";
    cout << "  \"status\": \"" << status << "\"";
    if (!error_msg.empty()) {
        cout << ",\n  \"error\": \"" << error_msg << "\"\n";
    } else {
        cout << "\n";
    }
    cout << "}\n";
}

vector<unsigned char> derive_key(const string& password, const vector<unsigned char>& salt) {
    vector<unsigned char> key(AES_256_KEY_SIZE);
    PKCS5_PBKDF2_HMAC(password.c_str(), password.length(),
                      salt.data(), salt.size(),
                      PBKDF2_ITERATIONS,
                      EVP_sha256(),
                      AES_256_KEY_SIZE, key.data());
    return key;
}

int do_encrypt(istream& in, ostream& out, const string& password, vector<unsigned char>& out_salt) {
    vector<unsigned char> salt(SALT_SIZE);
    RAND_bytes(salt.data(), SALT_SIZE);
    out_salt = salt;
    
    vector<unsigned char> iv(AES_GCM_IV_SIZE);
    RAND_bytes(iv.data(), AES_GCM_IV_SIZE);
    
    out.write((char*)salt.data(), SALT_SIZE);
    out.write((char*)iv.data(), AES_GCM_IV_SIZE);

    vector<unsigned char> key = derive_key(password, salt);

    EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
    EVP_EncryptInit_ex(ctx, EVP_aes_256_gcm(), NULL, NULL, NULL);
    EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, AES_GCM_IV_SIZE, NULL);
    EVP_EncryptInit_ex(ctx, NULL, NULL, key.data(), iv.data());

    unsigned char inbuf[8192];
    unsigned char outbuf[8192 + AES_BLOCK_SIZE];
    int outlen;

    while (in.read((char*)inbuf, sizeof(inbuf))) {
        int inlen = in.gcount();
        EVP_EncryptUpdate(ctx, outbuf, &outlen, inbuf, inlen);
        out.write((char*)outbuf, outlen);
    }
    if (in.gcount() > 0) {
        int inlen = in.gcount();
        EVP_EncryptUpdate(ctx, outbuf, &outlen, inbuf, inlen);
        out.write((char*)outbuf, outlen);
    }

    EVP_EncryptFinal_ex(ctx, outbuf, &outlen);
    out.write((char*)outbuf, outlen);

    unsigned char tag[AES_GCM_TAG_SIZE];
    EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_GET_TAG, AES_GCM_TAG_SIZE, tag);
    out.write((char*)tag, AES_GCM_TAG_SIZE);

    EVP_CIPHER_CTX_free(ctx);
    
    // Wipe key from memory
    OPENSSL_cleanse(key.data(), key.size());
    return 1;
}

int do_decrypt(const string& infile, const string& outfile, const string& password) {
    ifstream in(infile, ios::binary | ios::ate);
    if (!in) return -1;
    
    streamsize file_size = in.tellg();
    if (file_size < SALT_SIZE + AES_GCM_IV_SIZE + AES_GCM_TAG_SIZE) {
        return -2; // File too small inherently corrupt
    }
    
    in.seekg(0, ios::beg);

    vector<unsigned char> salt(SALT_SIZE);
    in.read((char*)salt.data(), SALT_SIZE);
    
    vector<unsigned char> iv(AES_GCM_IV_SIZE);
    in.read((char*)iv.data(), AES_GCM_IV_SIZE);

    ofstream out(outfile, ios::binary);
    if (!out) return -1;

    vector<unsigned char> key = derive_key(password, salt);

    EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
    EVP_DecryptInit_ex(ctx, EVP_aes_256_gcm(), NULL, NULL, NULL);
    EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, AES_GCM_IV_SIZE, NULL);
    EVP_DecryptInit_ex(ctx, NULL, NULL, key.data(), iv.data());

    // Calculate how much ciphertext we have 
    streamsize ciphertext_len = file_size - SALT_SIZE - AES_GCM_IV_SIZE - AES_GCM_TAG_SIZE;
    
    unsigned char inbuf[8192];
    unsigned char outbuf[8192 + AES_BLOCK_SIZE];
    int outlen;
    streamsize bytes_read = 0;

    while (bytes_read < ciphertext_len) {
        streamsize to_read = min((streamsize)sizeof(inbuf), ciphertext_len - bytes_read);
        in.read((char*)inbuf, to_read);
        EVP_DecryptUpdate(ctx, outbuf, &outlen, inbuf, to_read);
        out.write((char*)outbuf, outlen);
        bytes_read += to_read;
    }

    // Read Tag
    unsigned char tag[AES_GCM_TAG_SIZE];
    in.read((char*)tag, AES_GCM_TAG_SIZE);
    EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_TAG, AES_GCM_TAG_SIZE, tag);

    int ret = EVP_DecryptFinal_ex(ctx, outbuf, &outlen);
    
    OPENSSL_cleanse(key.data(), key.size());
    EVP_CIPHER_CTX_free(ctx);
    in.close();

    if (ret > 0) {
        out.write((char*)outbuf, outlen);
        out.close();
        return 1;
    } else {
        out.close();
        remove(outfile.c_str()); // Destruct the malformed output
        return 0; // Auth Failed!
    }
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        cerr << "Usage: encryptor <mode> [...]\n";
        return 1;
    }

    string mode = argv[1];

    if (mode == "sign_token") {
        if (argc < 6) return 1;
        string file_id = argv[2];
        long expiry_hours = stol(argv[3]);
        bool one_time = (string(argv[4]) == "1" || string(argv[4]) == "true");
        string secret = argv[5];

        time_t now = time(nullptr);
        time_t exp_time = now + (expiry_hours * 3600);

        string header = "{\"alg\":\"HS256\",\"typ\":\"JWT\"}";
        string payload = "{\"file_id\":\"" + file_id + "\",\"exp\":" + to_string(exp_time) + ",\"one_time\":" + (one_time?"true":"false") + "}";

        string msg = base64url_encode(header) + "." + base64url_encode(payload);
        string sig_b64 = base64url_encode(hmac_sha256(msg, secret));

        cout << "{\n  \"token\": \"" << msg << "." << sig_b64 << "\",\n  \"status\": \"success\"\n}\n";
        return 0;
    } 
    else if (mode == "verify_token") {
        if (argc < 4) return 1;
        string token = argv[2];
        string secret = argv[3];

        size_t p1 = token.find('.');
        size_t p2 = token.rfind('.');
        if (p1 == string::npos || p2 == string::npos || p1 == p2) {
            cout << "{\"error\":\"Malformed JWT\",\"status\":\"error\"}\n";
            return 1;
        }

        string msg = token.substr(0, p2);
        string sig = token.substr(p2 + 1);

        string expected_sig = base64url_encode(hmac_sha256(msg, secret));
        if (sig != expected_sig) {
            cout << "{\"error\":\"Signature Verification Failed\",\"status\":\"error\"}\n";
            return 1;
        }

        string payload = base64url_decode(token.substr(p1 + 1, p2 - p1 - 1));
        string file_id = str_extract(payload, "\"file_id\":\"", "\"");
        string exp_str = str_extract(payload, "\"exp\":", ",");
        
        if (exp_str.empty()) {
            // Backup parsing boundary incase one_time boolean parameter shifted the bracket 
            exp_str = str_extract(payload, "\"exp\":", "}");
        }

        long exp = exp_str.empty() ? 0 : stol(exp_str);
        if (time(nullptr) > exp) {
            cout << "{\"error\":\"Token Expired\",\"status\":\"error\"}\n";
            return 1;
        }

        cout << "{\n  \"file_id\": \"" << file_id << "\",\n  \"valid\": true,\n  \"status\": \"success\"\n}\n";
        return 0;
    }

    if (argc < 5) {
        cerr << "Usage: encryptor <encrypt|decrypt> <input|stdin> <output|stdout> <password>\n";
        return 1;
    }

    string infile = argv[2];
    string outfile = argv[3];
    string password = argv[4];

    if (mode == "encrypt") {
        vector<unsigned char> salt;
        bool success = false;
        
        if (infile == "stdin" && outfile == "stdout") {
            success = do_encrypt(cin, cout, password, salt);
            return success ? 0 : 1; 
        } else {
            ifstream in_fs(infile, ios::binary);
            ofstream out_fs(outfile, ios::binary);
            if (!in_fs || !out_fs) {
                print_json("", "", "error", "IO Failure");
                return 1;
            }
            success = do_encrypt(in_fs, out_fs, password, salt);
            in_fs.close();
            out_fs.close();
            
            if (success) {
                print_json(bytes_to_hex(salt), "", "success");
            } else {
                print_json("", "", "error", "Encryption Failed");
            }
        }
    } else if (mode == "decrypt") {
        int res = do_decrypt(infile, outfile, password);
        if (res == 1) {
            print_json("", "", "success");
        } else if (res == 0) {
            print_json("", "", "error", "Invalid Password or Corrupt Data (GCM Auth Failed)");
        } else {
            print_json("", "", "error", "File IO format intrinsically invalid");
        }
    }

    return 0;
}
