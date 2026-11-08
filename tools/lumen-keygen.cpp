// Gerador de licenças do Lumen (só para o autor: usa a chave privada).
//
//   lumen-keygen                  modo guiado (pergunta tudo)
//   lumen-keygen novas-chaves     cria a chave privada (uma vez só)
//   lumen-keygen licenca --nome "Loja Centro" --maquina 1A2B-3C4D-5E6F-7A8B
//                        [--maquina ...] [--validade 2027-12-31] [--id L-0001]
//                        [--chave arquivo]
//
// A chave privada fica em lumen-chave-privada.txt, ao lado deste programa.
// Quem tiver esse arquivo cria licenças: guarde uma cópia num lugar seguro
// (pen drive, gerenciador de senhas) e nunca o envie a ninguém.
#include "core/license.h"
#include "platform/platform.h"

#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#include <bcrypt.h>
#endif

using namespace lumen;

static const char *kPrivateHeader = "LUMEN-CHAVE-PRIVADA-1 ";

static std::string exeDir() { return platform::executableDir(); }
static std::string inExeDir(const std::string &name) {
  return platform::toUtf8(platform::toPath(exeDir()) / platform::toPath(name));
}

static bool randomBytes(uint8_t *out, size_t n) {
#ifdef _WIN32
  return BCryptGenRandom(nullptr, out, ULONG(n), BCRYPT_USE_SYSTEM_PREFERRED_RNG) == 0;
#else
  FILE *f = std::fopen("/dev/urandom", "rb");
  if (!f) return false;
  bool ok = std::fread(out, 1, n, f) == n;
  std::fclose(f);
  return ok;
#endif
}

static std::string readLine(const std::string &prompt) {
  std::cout << prompt << std::flush;
#ifdef _WIN32
  // The console hands typed accents over correctly only as UTF-16.
  HANDLE in = GetStdHandle(STD_INPUT_HANDLE);
  DWORD mode;
  if (GetConsoleMode(in, &mode)) {
    wchar_t buf[1024];
    DWORD got = 0;
    if (!ReadConsoleW(in, buf, 1023, &got, nullptr)) return {};
    std::wstring w(buf, got);
    while (!w.empty() && (w.back() == L'\n' || w.back() == L'\r')) w.pop_back();
    int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), int(w.size()), nullptr, 0, nullptr, nullptr);
    std::string s(size_t(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), int(w.size()), s.data(), n, nullptr, nullptr);
    return s;
  }
#endif
  std::string s;
  std::getline(std::cin, s);
  while (!s.empty() && (s.back() == '\r' || s.back() == '\n')) s.pop_back();
  return s;
}

static bool loadSeed(const std::string &file, uint8_t seed[32], std::string &err) {
  std::string text;
  if (!platform::readFile(file, text)) {
    err = "não encontrei a chave privada em " + file;
    return false;
  }
  size_t at = text.find(kPrivateHeader);
  std::vector<uint8_t> bytes;
  if (at == std::string::npos) {
    err = "o arquivo " + file + " não é uma chave privada do Lumen";
    return false;
  }
  std::string b64 = text.substr(at + std::strlen(kPrivateHeader));
  b64 = b64.substr(0, b64.find_first_of("\r\n "));
  if (!fromBase64(b64, bytes) || bytes.size() != 32) {
    err = "a chave privada em " + file + " está danificada";
    return false;
  }
  std::copy(bytes.begin(), bytes.end(), seed);
  return true;
}

static int newKeys(const std::string &file) {
  if (std::filesystem::exists(platform::toPath(file))) {
    std::cout << "Já existe uma chave privada em " << file << ".\n"
              << "Ela NÃO foi substituída: trocar de chave invalida todas as licenças já emitidas.\n";
    return 1;
  }
  uint8_t seed[32], sk[64], pk[32];
  if (!randomBytes(seed, sizeof(seed))) {
    std::cout << "Não foi possível obter números aleatórios do sistema.\n";
    return 1;
  }
  std::string text = std::string(kPrivateHeader) + toBase64(seed, 32) + "\n"
                     "# Chave privada das licenças do Lumen. Quem tiver este arquivo cria licenças.\n"
                     "# Guarde uma cópia num lugar seguro e NUNCA envie este arquivo a ninguém.\n";
  if (!platform::writeFile(file, text)) {
    std::cout << "Não foi possível gravar " << file << "\n";
    return 1;
  }
  licenseKeyPair(seed, sk, pk);
  std::string pub;
  for (int i = 0; i < 32; ++i) {
    char b[8];
    std::snprintf(b, sizeof(b), "0x%02x%s", pk[i], i == 31 ? "" : (i % 16 == 15 ? ",\n" : ", "));
    pub += b;
  }
  platform::writeFile(inExeDir("lumen-chave-publica.txt"), pub + "\n");
  std::cout << "Chave privada criada em " << file << "\n"
            << "Chave pública (vai dentro do Lumen; pode ser compartilhada) em "
            << inExeDir("lumen-chave-publica.txt") << "\n";
  return 0;
}

static std::string fileSafe(const std::string &s) {
  std::string out;
  for (unsigned char c : s) out += (std::isalnum(c) || c >= 0x80) ? char(c) : '_';
  return out.empty() ? std::string("licenca") : out;
}

static int issue(const std::string &keyFile, License l) {
  uint8_t seed[32], sk[64], pk[32];
  std::string err;
  if (!loadSeed(keyFile, seed, err)) {
    std::cout << "Erro: " << err << "\n";
    return 1;
  }
  licenseKeyPair(seed, sk, pk);
  if (std::memcmp(pk, builtinLicensePublicKey(), 32) != 0) {
    std::cout << "Erro: esta chave privada não é a que está dentro do Lumen.\n"
                 "As licenças feitas com ela não seriam aceitas.\n";
    return 1;
  }
  if (l.name.empty()) {
    std::cout << "Erro: falta o nome do cliente.\n";
    return 1;
  }
  std::vector<std::string> codes;
  for (auto &m : l.machines) {
    std::string c = normalizeMachineCode(m);
    if (c.empty()) {
      std::cout << "Erro: código de máquina inválido: \"" << m << "\" (são 16 letras/números, ex. 1A2B-3C4D-5E6F-7A8B)\n";
      return 1;
    }
    codes.push_back(c);
  }
  if (codes.empty()) {
    std::cout << "Erro: falta o código da máquina (o cliente vê em Ajuda > Licença).\n";
    return 1;
  }
  l.machines = codes;
  if (!l.expires.empty() && dayNumber(l.expires) < 0) {
    std::cout << "Erro: validade inválida \"" << l.expires << "\" (use AAAA-MM-DD, ex. 2027-12-31)\n";
    return 1;
  }
  l.issued = todayLocal();
  if (l.id.empty()) {
    char id[48];
    std::time_t now = std::time(nullptr);
    std::snprintf(id, sizeof(id), "L-%lld", (long long)(now % 100000000));
    l.id = id;
  }
  std::string text = makeLicense(l, sk);
  License check;
  if (checkLicense(text, codes.front(), l.issued, check) != LicenseCheck::Ok) {
    std::cout << "Erro interno: a licença criada não passou na verificação.\n";
    return 1;
  }
  std::string dir = inExeDir("licencas");
  std::error_code ec;
  std::filesystem::create_directories(platform::toPath(dir), ec);
  std::string file = platform::toUtf8(platform::toPath(dir) / platform::toPath(fileSafe(l.name) + "-" + l.id + ".lumenlic"));
  std::string body = "-----BEGIN LUMEN LICENSE-----\n" + text + "\n-----END LUMEN LICENSE-----\n";
  platform::writeFile(file, body);
  // A record of every licence issued.
  std::string logFile = inExeDir("licencas-emitidas.csv"), log;
  platform::readFile(logFile, log);
  if (log.empty()) log = "data;id;cliente;maquinas;validade\n";
  std::string machines;
  for (auto &c : codes) machines += (machines.empty() ? "" : " ") + c;
  log += l.issued + ";" + l.id + ";" + l.name + ";" + machines + ";" + (l.expires.empty() ? "sem validade" : l.expires) + "\n";
  platform::writeFile(logFile, log);

  std::cout << "\nLicença para \"" << l.name << "\" (" << l.id << "), máquina(s) " << machines
            << (l.expires.empty() ? ", sem validade" : ", válida até " + l.expires) << ":\n\n"
            << body << "\nArquivo: " << file << "\n"
            << "Envie ao cliente o arquivo .lumenlic (ou o texto acima). Ele abre em Ajuda > Licença.\n";
  return 0;
}

int main(int argc, char **argv) {
#ifdef _WIN32
  SetConsoleOutputCP(CP_UTF8);
#endif
  std::vector<std::string> args = platform::utf8Args(argc, argv);
  std::string keyFile = inExeDir("lumen-chave-privada.txt");
  if (args.size() >= 2 && args[1] == "novas-chaves") return newKeys(args.size() >= 3 ? args[2] : keyFile);
  if (args.size() >= 2 && args[1] == "licenca") {
    License l;
    for (size_t i = 2; i + 1 < args.size(); i += 2) {
      const std::string &k = args[i], &v = args[i + 1];
      if (k == "--nome") l.name = v;
      else if (k == "--maquina") l.machines.push_back(v);
      else if (k == "--validade") l.expires = v;
      else if (k == "--id") l.id = v;
      else if (k == "--chave") keyFile = v;
      else {
        std::cout << "Opção desconhecida: " << k << "\n";
        return 2;
      }
    }
    return issue(keyFile, l);
  }
  if (args.size() >= 2) {
    std::cout << "Uso:\n  lumen-keygen                 modo guiado\n  lumen-keygen novas-chaves\n"
                 "  lumen-keygen licenca --nome \"Cliente\" --maquina XXXX-XXXX-XXXX-XXXX [--maquina ...]\n"
                 "               [--validade AAAA-MM-DD] [--id L-0001] [--chave arquivo]\n";
    return 2;
  }

  // Guided mode (a double click on Windows).
  std::cout << "=== Gerador de licenças do Lumen ===\n\n";
  int code = 0;
  if (!std::filesystem::exists(platform::toPath(keyFile))) {
    std::cout << "Não há chave privada nesta pasta (" << keyFile << ").\n";
    std::string a = readLine("Criar uma agora? Faça isso UMA vez só (s/n): ");
    if (a == "s" || a == "S") code = newKeys(keyFile);
    readLine("\nPressione Enter para sair...");
    return code;
  }
  License l;
  l.name = readLine("Nome do cliente: ");
  std::string m = readLine("Código da máquina (vários: separe por vírgula): ");
  for (size_t at = 0; at <= m.size();) {
    size_t comma = m.find(',', at);
    std::string part = m.substr(at, comma == std::string::npos ? std::string::npos : comma - at);
    if (!part.empty()) l.machines.push_back(part);
    if (comma == std::string::npos) break;
    at = comma + 1;
  }
  l.expires = readLine("Validade AAAA-MM-DD (Enter = sem validade): ");
  l.id = readLine("Número da licença (Enter = automático): ");
  code = issue(keyFile, l);
  readLine("\nPressione Enter para sair...");
  return code;
}
