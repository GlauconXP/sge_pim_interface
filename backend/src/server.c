#include "mongoose.h"
#include <windows.h>
#include <sql.h>
#include <sqlext.h>
#include <stdio.h>
#include <string.h>
#include <limits.h>
#include <stdbool.h>
#include <bcrypt.h>
#include <time.h>
#include <ctype.h>

static SQLHENV ambiente = SQL_NULL_HENV;
static SQLHDBC conexao = SQL_NULL_HDBC;

#define TAMANHO_SALT 16
#define TAMANHO_HASH 32
#define ITERACOES_PBKDF2 150000ULL
#define MAX_SESSOES 64
#define DURACAO_SESSAO_SEGUNDOS (8 * 60 * 60)

typedef struct {
    char token[65];
    SQLINTEGER funcionarioId;
    char matricula[21];
    char nome[301];
    char cargo[201];
    bool senhaDeveTrocar;
    time_t ultimaAtividade;
} SessaoSGE;

static SessaoSGE sessoesSGE[MAX_SESSOES];

/* Permissões por cargo são verificadas no servidor, não apenas na interface. */
static int cargoEh(const SessaoSGE *sessao, const char *cargo) {
    return sessao != NULL && _stricmp(sessao->cargo, cargo) == 0;
}

static int podeGerenciarFuncionarios(const SessaoSGE *sessao) {
    return cargoEh(sessao, "Supervisora") ||
           cargoEh(sessao, "Supervisor") ||
           cargoEh(sessao, "Administrador");
}

static int podeCadastrarEncomendas(const SessaoSGE *sessao) {
    return podeGerenciarFuncionarios(sessao) || cargoEh(sessao, "Atendente");
}

static int podeAvancarPara(const SessaoSGE *sessao, const char *novoStatus) {
    if (podeGerenciarFuncionarios(sessao)) return 1;
    if (cargoEh(sessao, "Conferente")) {
        return strcmp(novoStatus, "Em triagem") == 0 ||
               strcmp(novoStatus, "Separada") == 0;
    }
    if (cargoEh(sessao, "Entregador")) {
        return strcmp(novoStatus, "Em distribuição") == 0 ||
               strcmp(novoStatus, "Entregue") == 0;
    }
    return 0;
}

static int transicaoStatusValida(const char *atual, const char *novo) {
    return (strcmp(atual, "Recebida") == 0 && strcmp(novo, "Em triagem") == 0) ||
           (strcmp(atual, "Em triagem") == 0 && strcmp(novo, "Separada") == 0) ||
           (strcmp(atual, "Separada") == 0 && strcmp(novo, "Em distribuição") == 0) ||
           (strcmp(atual, "Em distribuição") == 0 && strcmp(novo, "Entregue") == 0);
}

static void responderAcessoNegado(struct mg_connection *c) {
    mg_http_reply(c, 403,
        "Content-Type: application/json; charset=utf-8\r\n",
        "{\"erro\":\"Seu cargo não tem permissão para realizar esta operação.\"}\n");
}

static int gerarSaltSenha(unsigned char salt[TAMANHO_SALT]) {
    return BCryptGenRandom(NULL, salt, TAMANHO_SALT,
                           BCRYPT_USE_SYSTEM_PREFERRED_RNG) >= 0;
}

static int derivarHashSenha(const char *senha,
                            const unsigned char salt[TAMANHO_SALT],
                            unsigned char hash[TAMANHO_HASH]) {
    BCRYPT_ALG_HANDLE algoritmo = NULL;
    NTSTATUS estado = BCryptOpenAlgorithmProvider(
        &algoritmo, BCRYPT_SHA256_ALGORITHM, NULL,
        BCRYPT_ALG_HANDLE_HMAC_FLAG
    );
    if (estado < 0) return 0;

    estado = BCryptDeriveKeyPBKDF2(
        algoritmo,
        (PUCHAR) senha, (ULONG) strlen(senha),
        (PUCHAR) salt, TAMANHO_SALT,
        ITERACOES_PBKDF2,
        hash, TAMANHO_HASH, 0
    );
    BCryptCloseAlgorithmProvider(algoritmo, 0);
    return estado >= 0;
}

static int compararConstante(const unsigned char *a,
                             const unsigned char *b, size_t tamanho) {
    unsigned char diferenca = 0;
    for (size_t i = 0; i < tamanho; i++) diferenca |= (unsigned char)(a[i] ^ b[i]);
    return diferenca == 0;
}

static int converterWideUtf8(const SQLWCHAR *origem, char *destino, int capacidade) {
    int n = WideCharToMultiByte(CP_UTF8, 0, (LPCWCH) origem, -1,
                                destino, capacidade, NULL, NULL);
    return n > 0;
}

static int gerarTokenSessao(char token[65]) {
    unsigned char bytes[32];
    static const char hex[] = "0123456789abcdef";
    if (BCryptGenRandom(NULL, bytes, sizeof(bytes),
                        BCRYPT_USE_SYSTEM_PREFERRED_RNG) < 0) return 0;
    for (size_t i = 0; i < sizeof(bytes); i++) {
        token[i * 2] = hex[bytes[i] >> 4];
        token[i * 2 + 1] = hex[bytes[i] & 0x0f];
    }
    token[64] = '\0';
    return 1;
}

static SessaoSGE *criarSessao(SQLINTEGER id, const char *matricula,
                              const char *nome, const char *cargo, bool deveTrocar) {
    time_t agora = time(NULL);
    SessaoSGE *livre = NULL;
    for (int i = 0; i < MAX_SESSOES; i++) {
        if (sessoesSGE[i].token[0] != '\0' &&
            agora - sessoesSGE[i].ultimaAtividade >= DURACAO_SESSAO_SEGUNDOS) {
            memset(&sessoesSGE[i], 0, sizeof(sessoesSGE[i]));
        }
        if (livre == NULL && sessoesSGE[i].token[0] == '\0') livre = &sessoesSGE[i];
    }
    if (livre == NULL || !gerarTokenSessao(livre->token)) return NULL;
    livre->funcionarioId = id;
    snprintf(livre->matricula, sizeof(livre->matricula), "%s", matricula);
    snprintf(livre->nome, sizeof(livre->nome), "%s", nome);
    snprintf(livre->cargo, sizeof(livre->cargo), "%s", cargo != NULL ? cargo : "");
    livre->senhaDeveTrocar = deveTrocar;
    livre->ultimaAtividade = agora;
    return livre;
}

static int extrairTokenCookie(struct mg_http_message *requisicao, char token[65]) {
    struct mg_str *cookie = mg_http_get_header(requisicao, "Cookie");
    const char nomeCookie[] = "sge_session=";
    if (cookie == NULL || cookie->buf == NULL) return 0;
    size_t comprimentoNome = sizeof(nomeCookie) - 1;
    for (size_t i = 0; i + comprimentoNome + 64 <= cookie->len; i++) {
        if (memcmp(cookie->buf + i, nomeCookie, comprimentoNome) != 0) continue;
        const char *inicio = cookie->buf + i + comprimentoNome;
        int valido = 1;
        for (int j = 0; j < 64; j++) {
            if (!isxdigit((unsigned char) inicio[j])) { valido = 0; break; }
        }
        if (valido) {
            memcpy(token, inicio, 64);
            token[64] = '\0';
            return 1;
        }
    }
    return 0;
}

static SessaoSGE *obterSessao(struct mg_http_message *requisicao) {
    char token[65];
    if (!extrairTokenCookie(requisicao, token)) return NULL;
    time_t agora = time(NULL);
    for (int i = 0; i < MAX_SESSOES; i++) {
        SessaoSGE *sessao = &sessoesSGE[i];
        if (sessao->token[0] == '\0') continue;
        if (agora - sessao->ultimaAtividade >= DURACAO_SESSAO_SEGUNDOS) {
            memset(sessao, 0, sizeof(*sessao));
            continue;
        }
        if (strcmp(sessao->token, token) == 0) {
            sessao->ultimaAtividade = agora;
            return sessao;
        }
    }
    return NULL;
}

static void responderNaoAutenticado(struct mg_connection *c) {
    mg_http_reply(c, 401, "Content-Type: application/json; charset=utf-8\r\nCache-Control: no-store\r\n",
                  "{\"erro\":\"Sessão inválida ou expirada. Entre novamente.\"}\n");
}

static void mostrarErro(SQLSMALLINT tipo, SQLHANDLE handle) {
    SQLCHAR estado[6] = {0};
    SQLCHAR mensagem[1024] = {0};
    SQLINTEGER codigo;
    SQLSMALLINT tamanho;

    if (SQL_SUCCEEDED(SQLGetDiagRec(
        tipo, handle, 1, estado, &codigo,
        mensagem, sizeof(mensagem), &tamanho
    ))) {
        printf("Erro ODBC [%s, codigo %ld]: %s\n",
               estado, (long) codigo, mensagem);
        fflush(stdout);
    }
}

static int conectarBanco(void) {
    SQLRETURN resultado;

    SQLCHAR stringConexao[] =
        "DRIVER={ODBC Driver 18 for SQL Server};"
        "SERVER=GlauconXP;"
        "DATABASE=SGE_Encomendas;"
        "Trusted_Connection=Yes;"
        "Encrypt=Yes;"
        "TrustServerCertificate=Yes;";

    resultado = SQLAllocHandle(
        SQL_HANDLE_ENV, SQL_NULL_HANDLE, &ambiente
    );

    if (!SQL_SUCCEEDED(resultado)) {
        printf("Erro ao inicializar ODBC.\n");
        return 0;
    }

    resultado = SQLSetEnvAttr(
        ambiente, SQL_ATTR_ODBC_VERSION,
        (SQLPOINTER) SQL_OV_ODBC3, 0
    );

    if (!SQL_SUCCEEDED(resultado)) {
        mostrarErro(SQL_HANDLE_ENV, ambiente);
        return 0;
    }

    resultado = SQLAllocHandle(
        SQL_HANDLE_DBC, ambiente, &conexao
    );

    if (!SQL_SUCCEEDED(resultado)) {
        mostrarErro(SQL_HANDLE_ENV, ambiente);
        return 0;
    }

    resultado = SQLDriverConnect(
        conexao, NULL, stringConexao, SQL_NTS,
        NULL, 0, NULL, SQL_DRIVER_NOPROMPT
    );

    if (!SQL_SUCCEEDED(resultado)) {
        mostrarErro(SQL_HANDLE_DBC, conexao);
        return 0;
    }

    printf("Conectado ao SQL Server!\n");
    return 1;
}

/* Adiciona texto à resposta JSON, respeitando o limite do buffer. */
static int adicionarTexto(
    char *saida, size_t capacidade,
    size_t *posicao, const char *texto
) {
    size_t tamanho = strlen(texto);

    if (tamanho >= capacidade - *posicao) {
        return 0;
    }

    memcpy(saida + *posicao, texto, tamanho);
    *posicao += tamanho;
    saida[*posicao] = '\0';

    return 1;
}

/* Converte um texto em uma string JSON válida. */
static int adicionarJson(
    char *saida, size_t capacidade,
    size_t *posicao, const char *texto
) {
    if (!adicionarTexto(
        saida, capacidade, posicao, "\""
    )) {
        return 0;
    }

    for (const unsigned char *p =
             (const unsigned char *) texto; *p; p++) {
        char escape[7];

        switch (*p) {
            case '"':
                if (!adicionarTexto(
                    saida, capacidade, posicao, "\\\""
                )) return 0;
                break;

            case '\\':
                if (!adicionarTexto(
                    saida, capacidade, posicao, "\\\\"
                )) return 0;
                break;

            case '\n':
                if (!adicionarTexto(
                    saida, capacidade, posicao, "\\n"
                )) return 0;
                break;

            case '\r':
                if (!adicionarTexto(
                    saida, capacidade, posicao, "\\r"
                )) return 0;
                break;

            case '\t':
                if (!adicionarTexto(
                    saida, capacidade, posicao, "\\t"
                )) return 0;
                break;

            default:
                if (*p < 32) {
                    snprintf(
                        escape, sizeof(escape),
                        "\\u%04x", *p
                    );

                    if (!adicionarTexto(
                        saida, capacidade, posicao, escape
                    )) return 0;
                } else {
                    char caractere[2] = {(char)*p, '\0'};

                    if (!adicionarTexto(
                        saida, capacidade, posicao, caractere
                    )) return 0;
                }
        }
    }

    return adicionarTexto(
        saida, capacidade, posicao, "\""
    );
}

/* Lê texto como UTF-16 do SQL Server e o converte para UTF-8 para o JSON. */
static int lerColuna(
    SQLHSTMT consulta, SQLUSMALLINT coluna,
    SQLCHAR *buffer, SQLLEN capacidade
) {
    SQLWCHAR textoAmplo[512] = {0};
    SQLLEN tamanho;
    SQLRETURN resultado = SQLGetData(
        consulta, coluna, SQL_C_WCHAR,
        textoAmplo, sizeof(textoAmplo), &tamanho
    );

    if (!SQL_SUCCEEDED(resultado)) {
        mostrarErro(SQL_HANDLE_STMT, consulta);
        return 0;
    }

    if (tamanho == SQL_NULL_DATA) {
        buffer[0] = '\0';
        return 1;
    }

    textoAmplo[(sizeof(textoAmplo) / sizeof(textoAmplo[0])) - 1] = 0;

    if (capacidade <= 0 || capacidade > INT_MAX) {
        return 0;
    }

    int bytes = WideCharToMultiByte(
        CP_UTF8, 0,
        (LPCWCH) textoAmplo, -1,
        (LPSTR) buffer, (int) capacidade,
        NULL, NULL
    );

    if (bytes <= 0) {
        printf("Erro ao converter texto do SQL Server para UTF-8.\n");
        buffer[capacidade - 1] = '\0';
        return 0;
    }

    return 1;
}
static int converterCampoJson(
    struct mg_str corpo,
    const char *caminho,
    SQLWCHAR *destino,
    int capacidade
) {
    char *valor = mg_json_get_str(corpo, caminho);

    if (valor == NULL) {
        return 0;
    }

    int resultado = MultiByteToWideChar(
        CP_UTF8,
        0,
        valor,
        -1,
        (LPWSTR) destino,
        capacidade
    );

    mg_free(valor);

    return resultado > 0;
}
static void cadastrarEncomenda(
    struct mg_connection *c,
    struct mg_http_message *requisicao
) {
    SQLHSTMT consulta = SQL_NULL_HSTMT;
    SQLRETURN resultado;
    int transacaoAtiva = 0;
    SQLWCHAR codigo[64];
    SQLWCHAR destinatario[201];
    SQLWCHAR endereco[401];
    SQLWCHAR cep[20];
    SQLWCHAR bairro[161];
    SQLWCHAR cidade[161];
    SQLWCHAR tipo[61];
    SQLWCHAR status[61];
    SQLWCHAR prioridade[41];
    SQLWCHAR matriculaResponsavel[32] = {0};
    struct mg_str corpo = requisicao->body;

    char *matriculaResponsavelTexto = mg_json_get_str(corpo, "$.ownerMatricula");
    if (matriculaResponsavelTexto != NULL) {
        int convertido = MultiByteToWideChar(
            CP_UTF8, 0, matriculaResponsavelTexto, -1,
            (LPWSTR) matriculaResponsavel,
            (int) (sizeof(matriculaResponsavel) / sizeof(matriculaResponsavel[0]))
        );
        mg_free(matriculaResponsavelTexto);
        if (convertido <= 0) {
            mg_http_reply(
                c, 400, "Content-Type: application/json; charset=utf-8\r\n",
                "{\"erro\":\"Matrícula do funcionário inválida.\"}\n"
            );
            return;
        }
    }

    int valido =
        converterCampoJson(corpo, "$.code", codigo, 64) &&
        converterCampoJson(corpo, "$.recipient", destinatario, 201) &&
        converterCampoJson(corpo, "$.address", endereco, 401) &&
        converterCampoJson(corpo, "$.cep", cep, 20) &&
        converterCampoJson(corpo, "$.neighborhood", bairro, 161) &&
        converterCampoJson(corpo, "$.city", cidade, 161) &&
        converterCampoJson(corpo, "$.type", tipo, 61) &&
        converterCampoJson(corpo, "$.status", status, 61) &&
        converterCampoJson(corpo, "$.priority", prioridade, 41);

    if (!valido || codigo[0] == 0) {
        mg_http_reply(
            c, 400, "Content-Type: application/json; charset=utf-8\r\n",
            "{\"erro\":\"Preencha corretamente todos os campos.\"}\n"
        );
        return;
    }

    /* Se foi selecionado um responsável, confirme que existe e está ativo. */
    if (matriculaResponsavel[0] != 0) {
        SQLHSTMT validacaoFuncionario = SQL_NULL_HSTMT;
        resultado = SQLAllocHandle(
            SQL_HANDLE_STMT, conexao, &validacaoFuncionario
        );
        if (!SQL_SUCCEEDED(resultado)) {
            mostrarErro(SQL_HANDLE_DBC, conexao);
            mg_http_reply(
                c, 500, "Content-Type: application/json; charset=utf-8\r\n",
                "{\"erro\":\"Não foi possível validar o funcionário.\"}\n"
            );
            return;
        }

        SQLCHAR sqlValidacao[] =
            "SELECT Id FROM dbo.Funcionarios WHERE Matricula = ? AND Ativo = 1";
        resultado = SQLPrepare(validacaoFuncionario, sqlValidacao, SQL_NTS);
        SQLLEN tamanhoMatriculaValidacao = SQL_NTS;
        if (SQL_SUCCEEDED(resultado)) {
            resultado = SQLBindParameter(
                validacaoFuncionario, 1, SQL_PARAM_INPUT,
                SQL_C_WCHAR, SQL_WVARCHAR, 20, 0,
                matriculaResponsavel, sizeof(matriculaResponsavel),
                &tamanhoMatriculaValidacao
            );
        }
        if (SQL_SUCCEEDED(resultado)) {
            resultado = SQLExecute(validacaoFuncionario);
        }
        if (!SQL_SUCCEEDED(resultado)) {
            mostrarErro(SQL_HANDLE_STMT, validacaoFuncionario);
            SQLFreeHandle(SQL_HANDLE_STMT, validacaoFuncionario);
            mg_http_reply(
                c, 500, "Content-Type: application/json; charset=utf-8\r\n",
                "{\"erro\":\"Erro ao validar o funcionário no banco.\"}\n"
            );
            return;
        }
        resultado = SQLFetch(validacaoFuncionario);
        SQLFreeHandle(SQL_HANDLE_STMT, validacaoFuncionario);
        if (resultado == SQL_NO_DATA) {
            mg_http_reply(
                c, 400, "Content-Type: application/json; charset=utf-8\r\n",
                "{\"erro\":\"O funcionário selecionado não existe ou está inativo.\"}\n"
            );
            return;
        }
        if (!SQL_SUCCEEDED(resultado)) {
            mg_http_reply(
                c, 500, "Content-Type: application/json; charset=utf-8\r\n",
                "{\"erro\":\"Não foi possível validar o funcionário.\"}\n"
            );
            return;
        }
    }

    resultado = SQLSetConnectAttr(
        conexao, SQL_ATTR_AUTOCOMMIT,
        (SQLPOINTER) SQL_AUTOCOMMIT_OFF, 0
    );
    if (!SQL_SUCCEEDED(resultado)) {
        mostrarErro(SQL_HANDLE_DBC, conexao);
        mg_http_reply(
            c, 500, "Content-Type: application/json; charset=utf-8\r\n",
            "{\"erro\":\"Não foi possível iniciar a transação no banco.\"}\n"
        );
        return;
    }
    transacaoAtiva = 1;

    resultado = SQLAllocHandle(SQL_HANDLE_STMT, conexao, &consulta);
    if (!SQL_SUCCEEDED(resultado)) goto erro_sql_cadastro;

    {
        SQLCHAR sql[] =
            "INSERT INTO dbo.Encomendas "
            "(Codigo, Destinatario, Endereco, CEP, Bairro, "
            "Cidade, TipoEnvio, Status, Prioridade, FuncionarioResponsavelId) "
            "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, "
            "(SELECT Id FROM dbo.Funcionarios "
            "WHERE Matricula = NULLIF(?, N'') AND Ativo = 1))";
        resultado = SQLPrepare(consulta, sql, SQL_NTS);
    }
    if (!SQL_SUCCEEDED(resultado)) goto erro_sql_cadastro;

    SQLWCHAR *valores[] = {
        codigo, destinatario, endereco, cep, bairro,
        cidade, tipo, status, prioridade
    };
    SQLULEN tamanhosColuna[] = {30, 100, 200, 9, 80, 80, 30, 30, 20};
    SQLLEN comprimentos[9];

    for (int i = 0; i < 9; i++) {
        comprimentos[i] = SQL_NTS;
        resultado = SQLBindParameter(
            consulta,
            (SQLUSMALLINT)(i + 1),
            SQL_PARAM_INPUT,
            SQL_C_WCHAR,
            SQL_WVARCHAR,
            tamanhosColuna[i],
            0,
            valores[i],
            (SQLLEN)(
                i == 0 ? sizeof(codigo) :
                i == 1 ? sizeof(destinatario) :
                i == 2 ? sizeof(endereco) :
                i == 3 ? sizeof(cep) :
                i == 4 ? sizeof(bairro) :
                i == 5 ? sizeof(cidade) :
                i == 6 ? sizeof(tipo) :
                i == 7 ? sizeof(status) : sizeof(prioridade)
            ),
            &comprimentos[i]
        );
        if (!SQL_SUCCEEDED(resultado)) goto erro_sql_cadastro;
    }

    SQLLEN tamanhoMatriculaResponsavel = SQL_NTS;
    resultado = SQLBindParameter(
        consulta, 10, SQL_PARAM_INPUT, SQL_C_WCHAR, SQL_WVARCHAR,
        20, 0, matriculaResponsavel, sizeof(matriculaResponsavel),
        &tamanhoMatriculaResponsavel
    );
    if (!SQL_SUCCEEDED(resultado)) goto erro_sql_cadastro;

    resultado = SQLExecute(consulta);
    if (!SQL_SUCCEEDED(resultado)) goto erro_sql_cadastro;
    SQLFreeHandle(SQL_HANDLE_STMT, consulta);
    consulta = SQL_NULL_HSTMT;

    /* Registra o status inicial sem deixar encomenda e histórico desencontrados. */
    resultado = SQLAllocHandle(SQL_HANDLE_STMT, conexao, &consulta);
    if (!SQL_SUCCEEDED(resultado)) goto erro_sql_cadastro;

    {
        SQLCHAR sql[] =
            "INSERT INTO dbo.Movimentacoes (EncomendaId, Status, Observacao) "
            "SELECT Id, ?, N'Encomenda cadastrada no sistema' "
            "FROM dbo.Encomendas WHERE Codigo = ?";
        resultado = SQLPrepare(consulta, sql, SQL_NTS);
    }
    if (!SQL_SUCCEEDED(resultado)) goto erro_sql_cadastro;

    SQLLEN tamanhoStatusMov = SQL_NTS;
    SQLLEN tamanhoCodigoMov = SQL_NTS;
    resultado = SQLBindParameter(
        consulta, 1, SQL_PARAM_INPUT, SQL_C_WCHAR, SQL_WVARCHAR,
        30, 0, status, sizeof(status), &tamanhoStatusMov
    );
    if (!SQL_SUCCEEDED(resultado)) goto erro_sql_cadastro;
    resultado = SQLBindParameter(
        consulta, 2, SQL_PARAM_INPUT, SQL_C_WCHAR, SQL_WVARCHAR,
        30, 0, codigo, sizeof(codigo), &tamanhoCodigoMov
    );
    if (!SQL_SUCCEEDED(resultado)) goto erro_sql_cadastro;

    resultado = SQLExecute(consulta);
    if (!SQL_SUCCEEDED(resultado)) goto erro_sql_cadastro;
    SQLFreeHandle(SQL_HANDLE_STMT, consulta);
    consulta = SQL_NULL_HSTMT;

    resultado = SQLEndTran(SQL_HANDLE_DBC, conexao, SQL_COMMIT);
    if (!SQL_SUCCEEDED(resultado)) goto erro_sql_cadastro;
    transacaoAtiva = 0;

    resultado = SQLSetConnectAttr(
        conexao, SQL_ATTR_AUTOCOMMIT,
        (SQLPOINTER) SQL_AUTOCOMMIT_ON, 0
    );
    if (!SQL_SUCCEEDED(resultado)) mostrarErro(SQL_HANDLE_DBC, conexao);

    mg_http_reply(
        c, 201, "Content-Type: application/json; charset=utf-8\r\n",
        "{\"mensagem\":\"Encomenda cadastrada com sucesso.\"}\n"
    );
    return;

erro_sql_cadastro:
    if (consulta != SQL_NULL_HSTMT) {
        mostrarErro(SQL_HANDLE_STMT, consulta);
        SQLFreeHandle(SQL_HANDLE_STMT, consulta);
        consulta = SQL_NULL_HSTMT;
    } else {
        mostrarErro(SQL_HANDLE_DBC, conexao);
    }
    if (transacaoAtiva) {
        SQLRETURN rollback = SQLEndTran(SQL_HANDLE_DBC, conexao, SQL_ROLLBACK);
        if (!SQL_SUCCEEDED(rollback)) mostrarErro(SQL_HANDLE_DBC, conexao);
    }
    SQLSetConnectAttr(
        conexao, SQL_ATTR_AUTOCOMMIT,
        (SQLPOINTER) SQL_AUTOCOMMIT_ON, 0
    );
    mg_http_reply(
        c, 400, "Content-Type: application/json; charset=utf-8\r\n",
        "{\"erro\":\"Cadastro não realizado. Verifique se o código já existe e se os dados são válidos.\"}\n"
    );
}

static void listarEncomendas(struct mg_connection *c) {
    SQLHSTMT consulta = SQL_NULL_HSTMT;
    SQLRETURN resultado;
    int valido = 1;
    int primeira = 1;

    char json[65536] = {0};
    size_t posicao = 0;

    SQLCHAR codigo[128] = {0};
    SQLCHAR destinatario[256] = {0};
    SQLCHAR cidade[128] = {0};
    SQLCHAR status[64] = {0};
    SQLCHAR responsavel[256] = {0};

    if (!SQL_SUCCEEDED(SQLAllocHandle(
        SQL_HANDLE_STMT, conexao, &consulta
    ))) {
        mg_http_reply(
            c, 500, "Content-Type: application/json\r\n",
            "{\"erro\":\"Falha ao criar consulta\"}\n"
        );
        return;
    }

    SQLCHAR sql[] =
        "SELECT TOP (100) e.Codigo, e.Destinatario, e.Cidade, e.Status, "
        "f.Nome AS Responsavel "
        "FROM dbo.Encomendas AS e "
        "LEFT JOIN dbo.Funcionarios AS f ON f.Id = e.FuncionarioResponsavelId "
        "ORDER BY e.Id DESC";

    resultado = SQLExecDirect(consulta, sql, SQL_NTS);

    if (!SQL_SUCCEEDED(resultado)) {
        mostrarErro(SQL_HANDLE_STMT, consulta);
        SQLFreeHandle(SQL_HANDLE_STMT, consulta);

        mg_http_reply(
            c, 500, "Content-Type: application/json\r\n",
            "{\"erro\":\"Falha ao consultar encomendas\"}\n"
        );
        return;
    }

    valido = adicionarTexto(
        json, sizeof(json), &posicao, "["
    );

    while (valido) {
        resultado = SQLFetch(consulta);

        if (resultado == SQL_NO_DATA) {
            break;
        }

        if (!SQL_SUCCEEDED(resultado)) {
            mostrarErro(SQL_HANDLE_STMT, consulta);
            valido = 0;
            break;
        }

        if (!lerColuna(
                consulta, 1, codigo, sizeof(codigo)) ||
            !lerColuna(
                consulta, 2, destinatario, sizeof(destinatario)) ||
            !lerColuna(
                consulta, 3, cidade, sizeof(cidade)) ||
            !lerColuna(
                consulta, 4, status, sizeof(status)) ||
            !lerColuna(
                consulta, 5, responsavel, sizeof(responsavel))) {
            valido = 0;
            break;
        }

        if (!primeira) {
            valido = adicionarTexto(
                json, sizeof(json), &posicao, ","
            );
        }

        if (!valido) break;

        valido = adicionarTexto(
            json, sizeof(json), &posicao,
            "{\"codigo\":"
        );

        if (valido) valido = adicionarJson(
            json, sizeof(json), &posicao, (char *) codigo
        );

        if (valido) valido = adicionarTexto(
            json, sizeof(json), &posicao,
            ",\"destinatario\":"
        );

        if (valido) valido = adicionarJson(
            json, sizeof(json), &posicao,
            (char *) destinatario
        );

        if (valido) valido = adicionarTexto(
            json, sizeof(json), &posicao, ",\"cidade\":"
        );

        if (valido) valido = adicionarJson(
            json, sizeof(json), &posicao, (char *) cidade
        );

        if (valido) valido = adicionarTexto(
            json, sizeof(json), &posicao, ",\"status\":"
        );

        if (valido) valido = adicionarJson(
            json, sizeof(json), &posicao, (char *) status
        );

        if (valido) valido = adicionarTexto(
            json, sizeof(json), &posicao, ",\"responsavel\":"
        );

        if (valido) valido = adicionarJson(
            json, sizeof(json), &posicao, (char *) responsavel
        );

        if (valido) valido = adicionarTexto(
            json, sizeof(json), &posicao, "}"
        );

        primeira = 0;
    }

    if (valido) {
        valido = adicionarTexto(
            json, sizeof(json), &posicao, "]"
        );
    }

    SQLFreeHandle(SQL_HANDLE_STMT, consulta);

    if (!valido) {
        mg_http_reply(
            c, 500, "Content-Type: application/json\r\n",
            "{\"erro\":\"Falha ao montar resposta\"}\n"
        );
        return;
    }

    mg_http_reply(
        c, 200,
        "Content-Type: application/json; charset=utf-8\r\n",
        "%s\n", json
    );
}


/* Lista os funcionários cadastrados no SQL Server. */
static void listarFuncionarios(struct mg_connection *c) {
    SQLHSTMT consulta = SQL_NULL_HSTMT;
    SQLRETURN resultado;
    int valido = 1;
    int primeira = 1;
    char json[32768] = {0};
    size_t posicao = 0;
    SQLCHAR matricula[64] = {0};
    SQLCHAR nome[256] = {0};
    SQLCHAR cargo[128] = {0};
    SQLCHAR setor[128] = {0};
    SQLCHAR ativo[8] = {0};

    resultado = SQLAllocHandle(SQL_HANDLE_STMT, conexao, &consulta);
    if (!SQL_SUCCEEDED(resultado)) {
        mostrarErro(SQL_HANDLE_DBC, conexao);
        mg_http_reply(
            c, 500, "Content-Type: application/json; charset=utf-8\r\n",
            "{\"erro\":\"Não foi possível consultar os funcionários.\"}\n"
        );
        return;
    }

    SQLCHAR sql[] =
        "SELECT Matricula, Nome, COALESCE(Cargo, N''), COALESCE(Setor, N''), "
        "CASE WHEN Ativo = 1 THEN N'true' ELSE N'false' END "
        "FROM dbo.Funcionarios ORDER BY Nome";
    resultado = SQLExecDirect(consulta, sql, SQL_NTS);
    if (!SQL_SUCCEEDED(resultado)) {
        mostrarErro(SQL_HANDLE_STMT, consulta);
        SQLFreeHandle(SQL_HANDLE_STMT, consulta);
        mg_http_reply(
            c, 500, "Content-Type: application/json; charset=utf-8\r\n",
            "{\"erro\":\"Falha ao consultar funcionários no banco.\"}\n"
        );
        return;
    }

    valido = adicionarTexto(json, sizeof(json), &posicao, "[");
    while (valido) {
        resultado = SQLFetch(consulta);
        if (resultado == SQL_NO_DATA) break;
        if (!SQL_SUCCEEDED(resultado)) {
            mostrarErro(SQL_HANDLE_STMT, consulta);
            valido = 0;
            break;
        }
        if (!lerColuna(consulta, 1, matricula, sizeof(matricula)) ||
            !lerColuna(consulta, 2, nome, sizeof(nome)) ||
            !lerColuna(consulta, 3, cargo, sizeof(cargo)) ||
            !lerColuna(consulta, 4, setor, sizeof(setor)) ||
            !lerColuna(consulta, 5, ativo, sizeof(ativo))) {
            valido = 0;
            break;
        }
        if (!primeira) valido = adicionarTexto(json, sizeof(json), &posicao, ",");
        if (!valido) break;

        valido = adicionarTexto(json, sizeof(json), &posicao, "{\"matricula\":");
        if (valido) valido = adicionarJson(json, sizeof(json), &posicao, (char *) matricula);
        if (valido) valido = adicionarTexto(json, sizeof(json), &posicao, ",\"nome\":");
        if (valido) valido = adicionarJson(json, sizeof(json), &posicao, (char *) nome);
        if (valido) valido = adicionarTexto(json, sizeof(json), &posicao, ",\"cargo\":");
        if (valido) valido = adicionarJson(json, sizeof(json), &posicao, (char *) cargo);
        if (valido) valido = adicionarTexto(json, sizeof(json), &posicao, ",\"setor\":");
        if (valido) valido = adicionarJson(json, sizeof(json), &posicao, (char *) setor);
        if (valido) valido = adicionarTexto(json, sizeof(json), &posicao, ",\"ativo\":");
        if (valido) valido = adicionarTexto(json, sizeof(json), &posicao,
            strcmp((char *) ativo, "true") == 0 ? "true" : "false");
        if (valido) valido = adicionarTexto(json, sizeof(json), &posicao, "}");
        primeira = 0;
    }

    if (valido) valido = adicionarTexto(json, sizeof(json), &posicao, "]");
    SQLFreeHandle(SQL_HANDLE_STMT, consulta);
    if (!valido) {
        mg_http_reply(
            c, 500, "Content-Type: application/json; charset=utf-8\r\n",
            "{\"erro\":\"Não foi possível montar a lista de funcionários.\"}\n"
        );
        return;
    }
    mg_http_reply(
        c, 200, "Content-Type: application/json; charset=utf-8\r\n",
        "%s\n", json
    );
}

/* Cria funcionário com senha inicial armazenada somente como hash PBKDF2. */
static void cadastrarFuncionario(
    struct mg_connection *c,
    struct mg_http_message *requisicao
) {
    SQLHSTMT consulta = SQL_NULL_HSTMT;
    SQLRETURN resultado;
    struct mg_str corpo = requisicao->body;
    SQLWCHAR matricula[21] = {0}, nome[101] = {0};
    SQLWCHAR cargo[51] = {0}, setor[51] = {0};
    bool ativoJson = true;
    SQLCHAR ativo = 1, senhaDeveTrocar = 1;
    unsigned char salt[TAMANHO_SALT], hash[TAMANHO_HASH];
    char *senha = mg_json_get_str(corpo, "$.senha");

    if (!converterCampoJson(corpo, "$.matricula", matricula, 21) ||
        !converterCampoJson(corpo, "$.nome", nome, 101) ||
        !converterCampoJson(corpo, "$.cargo", cargo, 51) ||
        !converterCampoJson(corpo, "$.setor", setor, 51) ||
        !mg_json_get_bool(corpo, "$.ativo", &ativoJson) ||
        matricula[0] == 0 || nome[0] == 0 || cargo[0] == 0 || setor[0] == 0) {
        if (senha != NULL) mg_free(senha);
        mg_http_reply(c, 400, "Content-Type: application/json; charset=utf-8\r\n",
            "{\"erro\":\"Preencha matrícula, nome, cargo, setor e situação.\"}\n");
        return;
    }
    if (senha == NULL || strlen(senha) < 10 || strlen(senha) > 128) {
        if (senha != NULL) mg_free(senha);
        mg_http_reply(c, 400, "Content-Type: application/json; charset=utf-8\r\n",
            "{\"erro\":\"Defina uma senha inicial com pelo menos 10 caracteres.\"}\n");
        return;
    }
    if (!gerarSaltSenha(salt) || !derivarHashSenha(senha, salt, hash)) {
        mg_free(senha);
        mg_http_reply(c, 500, "Content-Type: application/json; charset=utf-8\r\n",
            "{\"erro\":\"Não foi possível proteger a senha.\"}\n");
        return;
    }
    mg_free(senha);
    ativo = ativoJson ? 1 : 0;

    resultado = SQLAllocHandle(SQL_HANDLE_STMT, conexao, &consulta);
    if (!SQL_SUCCEEDED(resultado)) {
        mostrarErro(SQL_HANDLE_DBC, conexao);
        mg_http_reply(c, 500, "Content-Type: application/json; charset=utf-8\r\n",
            "{\"erro\":\"Não foi possível preparar o cadastro do funcionário.\"}\n");
        return;
    }

    SQLCHAR sql[] =
        "INSERT INTO dbo.Funcionarios "
        "(Matricula, Nome, Cargo, Setor, Ativo, SenhaSalt, SenhaHash, SenhaDeveTrocar) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?)";
    resultado = SQLPrepare(consulta, sql, SQL_NTS);
    if (!SQL_SUCCEEDED(resultado)) goto erro_cadastrar_funcionario;

    SQLWCHAR *valores[] = {matricula, nome, cargo, setor};
    SQLULEN tamanhosColuna[] = {20, 100, 50, 50};
    SQLLEN tamanhosBuffer[] = {(SQLLEN)sizeof(matricula), (SQLLEN)sizeof(nome),
                               (SQLLEN)sizeof(cargo), (SQLLEN)sizeof(setor)};
    SQLLEN indicadores[4];
    for (int i = 0; i < 4; i++) {
        indicadores[i] = SQL_NTS;
        resultado = SQLBindParameter(consulta, (SQLUSMALLINT)(i + 1), SQL_PARAM_INPUT,
            SQL_C_WCHAR, SQL_WVARCHAR, tamanhosColuna[i], 0, valores[i],
            tamanhosBuffer[i], &indicadores[i]);
        if (!SQL_SUCCEEDED(resultado)) goto erro_cadastrar_funcionario;
    }
    SQLLEN indAtivo = 0;
    resultado = SQLBindParameter(consulta, 5, SQL_PARAM_INPUT, SQL_C_BIT, SQL_BIT,
                                 1, 0, &ativo, sizeof(ativo), &indAtivo);
    if (!SQL_SUCCEEDED(resultado)) goto erro_cadastrar_funcionario;
    SQLLEN indSalt = TAMANHO_SALT;
    resultado = SQLBindParameter(consulta, 6, SQL_PARAM_INPUT, SQL_C_BINARY, SQL_VARBINARY,
                                 TAMANHO_SALT, 0, salt, sizeof(salt), &indSalt);
    if (!SQL_SUCCEEDED(resultado)) goto erro_cadastrar_funcionario;
    SQLLEN indHash = TAMANHO_HASH;
    resultado = SQLBindParameter(consulta, 7, SQL_PARAM_INPUT, SQL_C_BINARY, SQL_VARBINARY,
                                 TAMANHO_HASH, 0, hash, sizeof(hash), &indHash);
    if (!SQL_SUCCEEDED(resultado)) goto erro_cadastrar_funcionario;
    SQLLEN indTrocar = 0;
    resultado = SQLBindParameter(consulta, 8, SQL_PARAM_INPUT, SQL_C_BIT, SQL_BIT,
                                 1, 0, &senhaDeveTrocar, sizeof(senhaDeveTrocar), &indTrocar);
    if (!SQL_SUCCEEDED(resultado)) goto erro_cadastrar_funcionario;

    resultado = SQLExecute(consulta);
    if (!SQL_SUCCEEDED(resultado)) goto erro_cadastrar_funcionario;
    SQLFreeHandle(SQL_HANDLE_STMT, consulta);
    mg_http_reply(c, 201, "Content-Type: application/json; charset=utf-8\r\n",
        "{\"mensagem\":\"Funcionário cadastrado. A senha deverá ser trocada no primeiro acesso.\"}\n");
    return;

erro_cadastrar_funcionario:
    mostrarErro(SQL_HANDLE_STMT, consulta);
    SQLFreeHandle(SQL_HANDLE_STMT, consulta);
    mg_http_reply(c, 409, "Content-Type: application/json; charset=utf-8\r\n",
        "{\"erro\":\"Não foi possível cadastrar. Confira se a matrícula já existe e se a senha atende aos requisitos.\"}\n");
}

/* Atualiza dados do funcionário; senha opcional redefine credenciais e exige troca no próximo acesso. */
static void atualizarFuncionario(
    struct mg_connection *c,
    struct mg_http_message *requisicao
) {
    SQLHSTMT consulta = SQL_NULL_HSTMT;
    SQLRETURN resultado;
    SQLLEN linhasAfetadas = -1;
    struct mg_str corpo = requisicao->body;
    SQLWCHAR matriculaOriginal[21] = {0}, matricula[21] = {0};
    SQLWCHAR nome[101] = {0}, cargo[51] = {0}, setor[51] = {0};
    bool ativoJson = true;
    SQLCHAR ativo = 1, alterarSenha = 0, senhaDeveTrocar = 1;
    unsigned char salt[TAMANHO_SALT] = {0}, hash[TAMANHO_HASH] = {0};
    char *novaSenha = mg_json_get_str(corpo, "$.senha");
    int definirSenha = novaSenha != NULL && novaSenha[0] != '\0';

    if (!converterCampoJson(corpo, "$.originalMatricula", matriculaOriginal, 21) ||
        !converterCampoJson(corpo, "$.matricula", matricula, 21) ||
        !converterCampoJson(corpo, "$.nome", nome, 101) ||
        !converterCampoJson(corpo, "$.cargo", cargo, 51) ||
        !converterCampoJson(corpo, "$.setor", setor, 51) ||
        !mg_json_get_bool(corpo, "$.ativo", &ativoJson) ||
        matriculaOriginal[0] == 0 || matricula[0] == 0 || nome[0] == 0 ||
        cargo[0] == 0 || setor[0] == 0) {
        if (novaSenha != NULL) mg_free(novaSenha);
        mg_http_reply(c, 400, "Content-Type: application/json; charset=utf-8\r\n",
            "{\"erro\":\"Dados do funcionário inválidos ou incompletos.\"}\n");
        return;
    }
    if (definirSenha) {
        if (strlen(novaSenha) < 10 || strlen(novaSenha) > 128) {
            mg_free(novaSenha);
            mg_http_reply(c, 400, "Content-Type: application/json; charset=utf-8\r\n",
                "{\"erro\":\"A nova senha deve ter entre 10 e 128 caracteres.\"}\n");
            return;
        }
        if (!gerarSaltSenha(salt) || !derivarHashSenha(novaSenha, salt, hash)) {
            mg_free(novaSenha);
            mg_http_reply(c, 500, "Content-Type: application/json; charset=utf-8\r\n",
                "{\"erro\":\"Não foi possível proteger a nova senha.\"}\n");
            return;
        }
    }
    if (novaSenha != NULL) mg_free(novaSenha);
    alterarSenha = definirSenha ? 1 : 0;
    ativo = ativoJson ? 1 : 0;

    resultado = SQLAllocHandle(SQL_HANDLE_STMT, conexao, &consulta);
    if (!SQL_SUCCEEDED(resultado)) {
        mostrarErro(SQL_HANDLE_DBC, conexao);
        mg_http_reply(c, 500, "Content-Type: application/json; charset=utf-8\r\n",
            "{\"erro\":\"Não foi possível preparar a atualização do funcionário.\"}\n");
        return;
    }
    SQLCHAR sql[] =
        "UPDATE dbo.Funcionarios SET Matricula=?, Nome=?, Cargo=?, Setor=?, Ativo=?, "
        "SenhaSalt=CASE WHEN ?=1 THEN ? ELSE SenhaSalt END, "
        "SenhaHash=CASE WHEN ?=1 THEN ? ELSE SenhaHash END, "
        "SenhaDeveTrocar=CASE WHEN ?=1 THEN 1 ELSE SenhaDeveTrocar END "
        "WHERE Matricula=?";
    resultado = SQLPrepare(consulta, sql, SQL_NTS);
    if (!SQL_SUCCEEDED(resultado)) goto erro_atualizar_funcionario;

    SQLWCHAR *valores[] = {matricula, nome, cargo, setor};
    SQLULEN tamanhosColuna[] = {20, 100, 50, 50};
    SQLLEN tamanhosBuffer[] = {(SQLLEN)sizeof(matricula), (SQLLEN)sizeof(nome),
                               (SQLLEN)sizeof(cargo), (SQLLEN)sizeof(setor)};
    SQLLEN indicadoresTexto[4];
    for (int i = 0; i < 4; i++) {
        indicadoresTexto[i] = SQL_NTS;
        resultado = SQLBindParameter(consulta, (SQLUSMALLINT)(i+1), SQL_PARAM_INPUT,
            SQL_C_WCHAR, SQL_WVARCHAR, tamanhosColuna[i], 0, valores[i],
            tamanhosBuffer[i], &indicadoresTexto[i]);
        if (!SQL_SUCCEEDED(resultado)) goto erro_atualizar_funcionario;
    }
    SQLLEN indAtivo = 0;
    resultado = SQLBindParameter(consulta, 5, SQL_PARAM_INPUT, SQL_C_BIT, SQL_BIT,
        1, 0, &ativo, sizeof(ativo), &indAtivo);
    if (!SQL_SUCCEEDED(resultado)) goto erro_atualizar_funcionario;
    SQLLEN indAlterar1 = 0;
    resultado = SQLBindParameter(consulta, 6, SQL_PARAM_INPUT, SQL_C_BIT, SQL_BIT,
        1, 0, &alterarSenha, sizeof(alterarSenha), &indAlterar1);
    if (!SQL_SUCCEEDED(resultado)) goto erro_atualizar_funcionario;
    SQLLEN indSalt = TAMANHO_SALT;
    resultado = SQLBindParameter(consulta, 7, SQL_PARAM_INPUT, SQL_C_BINARY, SQL_VARBINARY,
        TAMANHO_SALT, 0, salt, sizeof(salt), &indSalt);
    if (!SQL_SUCCEEDED(resultado)) goto erro_atualizar_funcionario;
    SQLLEN indAlterar2 = 0;
    resultado = SQLBindParameter(consulta, 8, SQL_PARAM_INPUT, SQL_C_BIT, SQL_BIT,
        1, 0, &alterarSenha, sizeof(alterarSenha), &indAlterar2);
    if (!SQL_SUCCEEDED(resultado)) goto erro_atualizar_funcionario;
    SQLLEN indHash = TAMANHO_HASH;
    resultado = SQLBindParameter(consulta, 9, SQL_PARAM_INPUT, SQL_C_BINARY, SQL_VARBINARY,
        TAMANHO_HASH, 0, hash, sizeof(hash), &indHash);
    if (!SQL_SUCCEEDED(resultado)) goto erro_atualizar_funcionario;
    SQLLEN indAlterar3 = 0;
    resultado = SQLBindParameter(consulta, 10, SQL_PARAM_INPUT, SQL_C_BIT, SQL_BIT,
        1, 0, &alterarSenha, sizeof(alterarSenha), &indAlterar3);
    if (!SQL_SUCCEEDED(resultado)) goto erro_atualizar_funcionario;
    SQLLEN indMatriculaOriginal = SQL_NTS;
    resultado = SQLBindParameter(consulta, 11, SQL_PARAM_INPUT, SQL_C_WCHAR, SQL_WVARCHAR,
        20, 0, matriculaOriginal, sizeof(matriculaOriginal), &indMatriculaOriginal);
    if (!SQL_SUCCEEDED(resultado)) goto erro_atualizar_funcionario;

    resultado = SQLExecute(consulta);
    if (!SQL_SUCCEEDED(resultado)) goto erro_atualizar_funcionario;
    SQLRowCount(consulta, &linhasAfetadas);
    SQLFreeHandle(SQL_HANDLE_STMT, consulta);
    if (linhasAfetadas == 0) {
        mg_http_reply(c, 404, "Content-Type: application/json; charset=utf-8\r\n",
            "{\"erro\":\"Funcionário não encontrado.\"}\n");
        return;
    }
    mg_http_reply(c, 200, "Content-Type: application/json; charset=utf-8\r\n",
        definirSenha
          ? "{\"mensagem\":\"Funcionário atualizado. A nova senha deverá ser trocada no próximo acesso.\"}\n"
          : "{\"mensagem\":\"Funcionário atualizado com sucesso.\"}\n");
    return;

erro_atualizar_funcionario:
    mostrarErro(SQL_HANDLE_STMT, consulta);
    SQLFreeHandle(SQL_HANDLE_STMT, consulta);
    mg_http_reply(c, 409, "Content-Type: application/json; charset=utf-8\r\n",
        "{\"erro\":\"Não foi possível atualizar o funcionário. Confira a matrícula e os dados.\"}\n");
}

static int statusPermitido(const char *status) {
    const char *permitidos[] = {
        "Recebida", "Em triagem", "Separada",
        "Em distribuição", "Entregue"
    };

    for (size_t i = 0; i < sizeof(permitidos) / sizeof(permitidos[0]); i++) {
        if (strcmp(status, permitidos[i]) == 0) {
            return 1;
        }
    }
    return 0;
}

/* Atualiza o status e grava uma movimentação na mesma transação. */
static void atualizarStatusEncomenda(
    struct mg_connection *c,
    struct mg_http_message *requisicao,
    SessaoSGE *sessao
) {
    SQLHSTMT consulta = SQL_NULL_HSTMT;
    SQLRETURN resultado;
    SQLLEN linhasAfetadas = -1;
    int transacaoAtiva = 0;
    const char *etapa = "validar os dados da requisição";
    SQLWCHAR codigo[64] = {0};
    SQLWCHAR status[61] = {0};
    char statusDesejadoTexto[61] = {0};
    SQLLEN tamanhoCodigo = SQL_NTS;
    SQLLEN tamanhoStatus = SQL_NTS;
    struct mg_str corpo = requisicao->body;
    char *statusTexto = mg_json_get_str(corpo, "$.status");

    if (statusTexto == NULL || !statusPermitido(statusTexto)) {
        if (statusTexto != NULL) mg_free(statusTexto);
        mg_http_reply(
            c, 400, "Content-Type: application/json; charset=utf-8\r\n",
            "{\"erro\":\"Status ausente ou inválido.\"}\n"
        );
        return;
    }
    snprintf(statusDesejadoTexto, sizeof(statusDesejadoTexto), "%s", statusTexto);
    if (!podeAvancarPara(sessao, statusTexto)) {
        mg_free(statusTexto);
        responderAcessoNegado(c);
        return;
    }
    mg_free(statusTexto);

    if (!converterCampoJson(corpo, "$.code", codigo, 64) || codigo[0] == 0 ||
        !converterCampoJson(corpo, "$.status", status, 61)) {
        mg_http_reply(
            c, 400, "Content-Type: application/json; charset=utf-8\r\n",
            "{\"erro\":\"Informe o código da encomenda e o status.\"}\n"
        );
        return;
    }

    etapa = "iniciar a transação SQL";
    resultado = SQLSetConnectAttr(
        conexao, SQL_ATTR_AUTOCOMMIT,
        (SQLPOINTER) SQL_AUTOCOMMIT_OFF, 0
    );
    if (!SQL_SUCCEEDED(resultado)) {
        mostrarErro(SQL_HANDLE_DBC, conexao);
        mg_http_reply(
            c, 500, "Content-Type: application/json; charset=utf-8\r\n",
            "{\"erro\":\"Não foi possível iniciar a transação no banco.\"}\n"
        );
        return;
    }
    transacaoAtiva = 1;

    /* Confere a etapa atual no banco antes de permitir a próxima transição. */
    SQLCHAR statusAtual[128] = {0};
    SQLWCHAR statusAtualW[61] = {0};
    SQLLEN indStatusAtual = 0;
    etapa = "consultar o status atual da encomenda";
    resultado = SQLAllocHandle(SQL_HANDLE_STMT, conexao, &consulta);
    if (!SQL_SUCCEEDED(resultado)) goto erro_sql_status;
    {
        SQLCHAR sql[] = "SELECT Status FROM dbo.Encomendas WHERE Codigo = ?";
        resultado = SQLPrepare(consulta, sql, SQL_NTS);
    }
    if (!SQL_SUCCEEDED(resultado)) goto erro_sql_status;
    etapa = "vincular o código para consultar o status atual";
    resultado = SQLBindParameter(consulta, 1, SQL_PARAM_INPUT, SQL_C_WCHAR,
        SQL_WVARCHAR, 30, 0, codigo, sizeof(codigo), &tamanhoCodigo);
    if (!SQL_SUCCEEDED(resultado)) goto erro_sql_status;
    etapa = "consultar o status atual no SQL Server";
    resultado = SQLExecute(consulta);
    if (!SQL_SUCCEEDED(resultado)) goto erro_sql_status;
    resultado = SQLFetch(consulta);
    if (resultado == SQL_NO_DATA) goto encomenda_nao_encontrada;
    if (!SQL_SUCCEEDED(resultado)) goto erro_sql_status;
    resultado = SQLGetData(consulta, 1, SQL_C_WCHAR, statusAtualW,
        sizeof(statusAtualW), &indStatusAtual);
    if (!SQL_SUCCEEDED(resultado) || indStatusAtual == SQL_NULL_DATA ||
        !converterWideUtf8(statusAtualW, (char *) statusAtual, sizeof(statusAtual))) {
        goto erro_sql_status;
    }
    SQLFreeHandle(SQL_HANDLE_STMT, consulta);
    consulta = SQL_NULL_HSTMT;
    if (!transicaoStatusValida((char *) statusAtual, statusDesejadoTexto)) {
        goto transicao_invalida;
    }

    /* Para encomendas antigas sem histórico, guarda o status anterior antes da atualização. */
    etapa = "preparar o registro do status anterior";
    resultado = SQLAllocHandle(SQL_HANDLE_STMT, conexao, &consulta);
    if (!SQL_SUCCEEDED(resultado)) goto erro_sql_status;
    {
        SQLCHAR sql[] =
            "INSERT INTO dbo.Movimentacoes (EncomendaId, Status, Observacao) "
            "SELECT e.Id, e.Status, N'Status anterior registrado ao habilitar o histórico' "
            "FROM dbo.Encomendas AS e WHERE e.Codigo = ? "
            "AND NOT EXISTS (SELECT 1 FROM dbo.Movimentacoes AS m WHERE m.EncomendaId = e.Id)";
        etapa = "preparar o SQL do status anterior";
        resultado = SQLPrepare(consulta, sql, SQL_NTS);
    }
    if (!SQL_SUCCEEDED(resultado)) goto erro_sql_status;
    etapa = "vincular o código para registrar o status anterior";
    resultado = SQLBindParameter(
        consulta, 1, SQL_PARAM_INPUT, SQL_C_WCHAR, SQL_WVARCHAR,
        30, 0, codigo, sizeof(codigo), &tamanhoCodigo
    );
    if (!SQL_SUCCEEDED(resultado)) goto erro_sql_status;
    etapa = "executar o registro do status anterior";
    resultado = SQLExecute(consulta);
    /* SQL_NO_DATA (100) é esperado quando a encomenda já tem histórico:
       o INSERT ... SELECT não encontrou uma linha nova para registrar. */
    if (!SQL_SUCCEEDED(resultado) && resultado != SQL_NO_DATA) {
        goto erro_sql_status;
    }
    SQLFreeHandle(SQL_HANDLE_STMT, consulta);
    consulta = SQL_NULL_HSTMT;

    etapa = "preparar a atualização da encomenda";
    resultado = SQLAllocHandle(SQL_HANDLE_STMT, conexao, &consulta);
    if (!SQL_SUCCEEDED(resultado)) goto erro_sql_status;

    {
        SQLCHAR sql[] =
            "UPDATE dbo.Encomendas SET Status = ? WHERE Codigo = ?";
        etapa = "preparar o comando UPDATE";
        resultado = SQLPrepare(consulta, sql, SQL_NTS);
        if (!SQL_SUCCEEDED(resultado)) goto erro_sql_status;
    }

    etapa = "vincular o novo status ao UPDATE";
    resultado = SQLBindParameter(
        consulta, 1, SQL_PARAM_INPUT, SQL_C_WCHAR, SQL_WVARCHAR,
        30, 0, status, sizeof(status), &tamanhoStatus
    );
    if (!SQL_SUCCEEDED(resultado)) goto erro_sql_status;

    etapa = "vincular o código ao UPDATE";
    resultado = SQLBindParameter(
        consulta, 2, SQL_PARAM_INPUT, SQL_C_WCHAR, SQL_WVARCHAR,
        30, 0, codigo, sizeof(codigo), &tamanhoCodigo
    );
    if (!SQL_SUCCEEDED(resultado)) goto erro_sql_status;

    etapa = "executar o UPDATE da encomenda";
    resultado = SQLExecute(consulta);
    if (!SQL_SUCCEEDED(resultado)) goto erro_sql_status;

    etapa = "verificar quantas encomendas foram atualizadas";
    resultado = SQLRowCount(consulta, &linhasAfetadas);
    if (!SQL_SUCCEEDED(resultado)) goto erro_sql_status;

    /* Alguns drivers ODBC retornam -1 quando a contagem de linhas é desconhecida.
       Isso, isoladamente, não significa que o UPDATE falhou.
       Só tratamos zero como encomenda não encontrada. */
    if (linhasAfetadas == 0) goto encomenda_nao_encontrada;

    SQLFreeHandle(SQL_HANDLE_STMT, consulta);
    consulta = SQL_NULL_HSTMT;

    etapa = "preparar o registro da nova movimentação";
    resultado = SQLAllocHandle(SQL_HANDLE_STMT, conexao, &consulta);
    if (!SQL_SUCCEEDED(resultado)) goto erro_sql_status;

    {
        SQLCHAR sql[] =
            "INSERT INTO dbo.Movimentacoes (EncomendaId, Status, Observacao) "
            "SELECT Id, ?, N'Status atualizado pela interface web' "
            "FROM dbo.Encomendas WHERE Codigo = ?";
        etapa = "preparar o INSERT da nova movimentação";
        resultado = SQLPrepare(consulta, sql, SQL_NTS);
        if (!SQL_SUCCEEDED(resultado)) goto erro_sql_status;
    }

    etapa = "vincular o status à nova movimentação";
    resultado = SQLBindParameter(
        consulta, 1, SQL_PARAM_INPUT, SQL_C_WCHAR, SQL_WVARCHAR,
        30, 0, status, sizeof(status), &tamanhoStatus
    );
    if (!SQL_SUCCEEDED(resultado)) goto erro_sql_status;

    etapa = "vincular o código à nova movimentação";
    resultado = SQLBindParameter(
        consulta, 2, SQL_PARAM_INPUT, SQL_C_WCHAR, SQL_WVARCHAR,
        30, 0, codigo, sizeof(codigo), &tamanhoCodigo
    );
    if (!SQL_SUCCEEDED(resultado)) goto erro_sql_status;

    etapa = "executar o INSERT da nova movimentação";
    resultado = SQLExecute(consulta);
    if (!SQL_SUCCEEDED(resultado)) goto erro_sql_status;

    SQLFreeHandle(SQL_HANDLE_STMT, consulta);
    consulta = SQL_NULL_HSTMT;

    etapa = "confirmar a transação no SQL Server";
    resultado = SQLEndTran(SQL_HANDLE_DBC, conexao, SQL_COMMIT);
    if (!SQL_SUCCEEDED(resultado)) goto erro_sql_status;
    transacaoAtiva = 0;

    resultado = SQLSetConnectAttr(
        conexao, SQL_ATTR_AUTOCOMMIT,
        (SQLPOINTER) SQL_AUTOCOMMIT_ON, 0
    );
    if (!SQL_SUCCEEDED(resultado)) mostrarErro(SQL_HANDLE_DBC, conexao);

    mg_http_reply(
        c, 200, "Content-Type: application/json; charset=utf-8\r\n",
        "{\"mensagem\":\"Status atualizado e movimentação registrada.\"}\n"
    );
    return;

transicao_invalida:
    if (consulta != SQL_NULL_HSTMT) {
        SQLFreeHandle(SQL_HANDLE_STMT, consulta);
        consulta = SQL_NULL_HSTMT;
    }
    SQLEndTran(SQL_HANDLE_DBC, conexao, SQL_ROLLBACK);
    transacaoAtiva = 0;
    SQLSetConnectAttr(conexao, SQL_ATTR_AUTOCOMMIT, (SQLPOINTER) SQL_AUTOCOMMIT_ON, 0);
    mg_http_reply(c, 409, "Content-Type: application/json; charset=utf-8\r\n",
        "{\"erro\":\"A encomenda só pode avançar para a próxima etapa do fluxo.\"}\n");
    return;

encomenda_nao_encontrada:
    if (consulta != SQL_NULL_HSTMT) {
        SQLFreeHandle(SQL_HANDLE_STMT, consulta);
        consulta = SQL_NULL_HSTMT;
    }
    SQLEndTran(SQL_HANDLE_DBC, conexao, SQL_ROLLBACK);
    transacaoAtiva = 0;
    SQLSetConnectAttr(
        conexao, SQL_ATTR_AUTOCOMMIT,
        (SQLPOINTER) SQL_AUTOCOMMIT_ON, 0
    );
    mg_http_reply(
        c, 404, "Content-Type: application/json; charset=utf-8\r\n",
        "{\"erro\":\"Encomenda não encontrada.\"}\n"
    );
    return;

erro_sql_status:
    fprintf(stderr, "\n[SGE] Falha ao avancar encomenda na etapa: %s (retorno ODBC %d).\n",
            etapa, (int) resultado);
    fflush(stderr);
    if (consulta != SQL_NULL_HSTMT) {
        mostrarErro(SQL_HANDLE_STMT, consulta);
        SQLFreeHandle(SQL_HANDLE_STMT, consulta);
        consulta = SQL_NULL_HSTMT;
    } else {
        mostrarErro(SQL_HANDLE_DBC, conexao);
    }
    if (transacaoAtiva) {
        SQLRETURN rollback = SQLEndTran(SQL_HANDLE_DBC, conexao, SQL_ROLLBACK);
        if (!SQL_SUCCEEDED(rollback)) mostrarErro(SQL_HANDLE_DBC, conexao);
    }
    SQLSetConnectAttr(
        conexao, SQL_ATTR_AUTOCOMMIT,
        (SQLPOINTER) SQL_AUTOCOMMIT_ON, 0
    );
    mg_http_reply(
        c, 500, "Content-Type: application/json; charset=utf-8\r\n",
        "{\"erro\":\"Falha na etapa: %s. Confira o terminal para o detalhe ODBC.\"}\n",
        etapa
    );
}

/* Retorna as movimentações persistidas para uma encomenda. */
static void listarHistoricoEncomenda(
    struct mg_connection *c,
    struct mg_http_message *requisicao
) {
    SQLHSTMT consulta = SQL_NULL_HSTMT;
    SQLRETURN resultado;
    char codigoUtf8[128] = {0};
    SQLWCHAR codigo[128] = {0};
    SQLLEN tamanhoCodigo = SQL_NTS;
    SQLCHAR status[128] = {0};
    SQLCHAR dataHora[64] = {0};
    SQLCHAR observacao[512] = {0};
    char json[32768] = {0};
    size_t posicao = 0;
    int primeira = 1;
    int valido;

    if (mg_http_get_var(
        &requisicao->query, "codigo", codigoUtf8, sizeof(codigoUtf8)
    ) <= 0 || codigoUtf8[0] == '\0') {
        mg_http_reply(
            c, 400, "Content-Type: application/json; charset=utf-8\r\n",
            "{\"erro\":\"Informe o código da encomenda na consulta.\"}\n"
        );
        return;
    }

    if (MultiByteToWideChar(
        CP_UTF8, 0, codigoUtf8, -1, (LPWSTR) codigo,
        (int) (sizeof(codigo) / sizeof(codigo[0]))
    ) <= 0) {
        mg_http_reply(
            c, 400, "Content-Type: application/json; charset=utf-8\r\n",
            "{\"erro\":\"O código informado é inválido.\"}\n"
        );
        return;
    }

    resultado = SQLAllocHandle(SQL_HANDLE_STMT, conexao, &consulta);
    if (!SQL_SUCCEEDED(resultado)) {
        mostrarErro(SQL_HANDLE_DBC, conexao);
        mg_http_reply(
            c, 500, "Content-Type: application/json; charset=utf-8\r\n",
            "{\"erro\":\"Falha ao consultar o histórico.\"}\n"
        );
        return;
    }

    {
        SQLCHAR sql[] =
            "SELECT m.Status, CONVERT(nvarchar(19), m.DataHora, 120), "
            "COALESCE(m.Observacao, N'') "
            "FROM dbo.Movimentacoes AS m "
            "INNER JOIN dbo.Encomendas AS e ON e.Id = m.EncomendaId "
            "WHERE e.Codigo = ? ORDER BY m.DataHora ASC, m.Id ASC";
        resultado = SQLPrepare(consulta, sql, SQL_NTS);
    }
    if (!SQL_SUCCEEDED(resultado)) goto erro_historico;

    resultado = SQLBindParameter(
        consulta, 1, SQL_PARAM_INPUT, SQL_C_WCHAR, SQL_WVARCHAR,
        30, 0, codigo, sizeof(codigo), &tamanhoCodigo
    );
    if (!SQL_SUCCEEDED(resultado)) goto erro_historico;

    resultado = SQLExecute(consulta);
    if (!SQL_SUCCEEDED(resultado)) goto erro_historico;

    valido = adicionarTexto(json, sizeof(json), &posicao, "[");
    while (valido) {
        resultado = SQLFetch(consulta);
        if (resultado == SQL_NO_DATA) break;
        if (!SQL_SUCCEEDED(resultado)) {
            mostrarErro(SQL_HANDLE_STMT, consulta);
            valido = 0;
            break;
        }

        if (!lerColuna(consulta, 1, status, sizeof(status)) ||
            !lerColuna(consulta, 2, dataHora, sizeof(dataHora)) ||
            !lerColuna(consulta, 3, observacao, sizeof(observacao))) {
            valido = 0;
            break;
        }

        if (!primeira) valido = adicionarTexto(json, sizeof(json), &posicao, ",");
        if (!valido) break;

        valido = adicionarTexto(json, sizeof(json), &posicao, "{\"status\":");
        if (valido) valido = adicionarJson(json, sizeof(json), &posicao, (char *) status);
        if (valido) valido = adicionarTexto(json, sizeof(json), &posicao, ",\"dataHora\":");
        if (valido) valido = adicionarJson(json, sizeof(json), &posicao, (char *) dataHora);
        if (valido) valido = adicionarTexto(json, sizeof(json), &posicao, ",\"observacao\":");
        if (valido) valido = adicionarJson(json, sizeof(json), &posicao, (char *) observacao);
        if (valido) valido = adicionarTexto(json, sizeof(json), &posicao, "}");
        primeira = 0;
    }

    if (valido) valido = adicionarTexto(json, sizeof(json), &posicao, "]");
    SQLFreeHandle(SQL_HANDLE_STMT, consulta);

    if (!valido) {
        mg_http_reply(
            c, 500, "Content-Type: application/json; charset=utf-8\r\n",
            "{\"erro\":\"Não foi possível montar o histórico.\"}\n"
        );
        return;
    }

    mg_http_reply(
        c, 200, "Content-Type: application/json; charset=utf-8\r\n",
        "%s\n", json
    );
    return;

erro_historico:
    mostrarErro(SQL_HANDLE_STMT, consulta);
    SQLFreeHandle(SQL_HANDLE_STMT, consulta);
    mg_http_reply(
        c, 500, "Content-Type: application/json; charset=utf-8\r\n",
        "{\"erro\":\"Falha ao consultar o histórico.\"}\n"
    );
}

static void responderErroLogin(struct mg_connection *c) {
    mg_http_reply(c, 401, "Content-Type: application/json; charset=utf-8\r\nCache-Control: no-store\r\n",
        "{\"erro\":\"Matrícula ou senha inválida, conta inativa ou credenciais ainda não configuradas.\"}\n");
}

static void autenticarUsuario(struct mg_connection *c, struct mg_http_message *requisicao) {
    SQLHSTMT consulta = SQL_NULL_HSTMT;
    SQLRETURN resultado;
    struct mg_str corpo = requisicao->body;
    SQLWCHAR matriculaW[21] = {0}, nomeW[151] = {0}, cargoW[101] = {0};
    SQLINTEGER funcionarioId = 0;
    SQLCHAR ativo = 0, deveTrocar = 0;
    unsigned char salt[TAMANHO_SALT] = {0}, hashBanco[TAMANHO_HASH] = {0};
    unsigned char hashTentativa[TAMANHO_HASH] = {0};
    SQLLEN indId = 0, indNome = 0, indAtivo = 0, indSalt = 0, indHash = 0, indTrocar = 0, indCargo = 0;
    char *matricula = mg_json_get_str(corpo, "$.matricula");
    char *senha = mg_json_get_str(corpo, "$.senha");

    if (matricula == NULL || senha == NULL || matricula[0] == '\0' ||
        senha[0] == '\0' || strlen(matricula) > 20 || strlen(senha) > 128 ||
        !converterCampoJson(corpo, "$.matricula", matriculaW, 21)) {
        if (matricula != NULL) mg_free(matricula);
        if (senha != NULL) mg_free(senha);
        mg_http_reply(c, 400, "Content-Type: application/json; charset=utf-8\r\n",
            "{\"erro\":\"Informe matrícula e senha.\"}\n");
        return;
    }

    resultado = SQLAllocHandle(SQL_HANDLE_STMT, conexao, &consulta);
    if (!SQL_SUCCEEDED(resultado)) goto erro_autenticacao;
    SQLCHAR sql[] = "SELECT Id, Nome, Ativo, SenhaSalt, SenhaHash, SenhaDeveTrocar, COALESCE(Cargo, N'') "
                    "FROM dbo.Funcionarios WHERE Matricula = ?";
    resultado = SQLPrepare(consulta, sql, SQL_NTS);
    if (!SQL_SUCCEEDED(resultado)) goto erro_autenticacao;
    SQLLEN indMatricula = SQL_NTS;
    resultado = SQLBindParameter(consulta, 1, SQL_PARAM_INPUT, SQL_C_WCHAR,
        SQL_WVARCHAR, 20, 0, matriculaW, sizeof(matriculaW), &indMatricula);
    if (!SQL_SUCCEEDED(resultado)) goto erro_autenticacao;
    resultado = SQLExecute(consulta);
    if (!SQL_SUCCEEDED(resultado)) goto erro_autenticacao;
    resultado = SQLFetch(consulta);
    if (resultado == SQL_NO_DATA || !SQL_SUCCEEDED(resultado)) {
        SQLFreeHandle(SQL_HANDLE_STMT, consulta);
        mg_free(matricula); mg_free(senha);
        responderErroLogin(c);
        return;
    }
    resultado = SQLGetData(consulta, 1, SQL_C_SLONG, &funcionarioId, sizeof(funcionarioId), &indId);
    if (!SQL_SUCCEEDED(resultado)) goto erro_autenticacao;
    resultado = SQLGetData(consulta, 2, SQL_C_WCHAR, nomeW, sizeof(nomeW), &indNome);
    if (!SQL_SUCCEEDED(resultado)) goto erro_autenticacao;
    resultado = SQLGetData(consulta, 3, SQL_C_BIT, &ativo, sizeof(ativo), &indAtivo);
    if (!SQL_SUCCEEDED(resultado)) goto erro_autenticacao;
    resultado = SQLGetData(consulta, 4, SQL_C_BINARY, salt, sizeof(salt), &indSalt);
    if (!SQL_SUCCEEDED(resultado)) goto erro_autenticacao;
    resultado = SQLGetData(consulta, 5, SQL_C_BINARY, hashBanco, sizeof(hashBanco), &indHash);
    if (!SQL_SUCCEEDED(resultado)) goto erro_autenticacao;
    resultado = SQLGetData(consulta, 6, SQL_C_BIT, &deveTrocar, sizeof(deveTrocar), &indTrocar);
    if (!SQL_SUCCEEDED(resultado)) goto erro_autenticacao;
    resultado = SQLGetData(consulta, 7, SQL_C_WCHAR, cargoW, sizeof(cargoW), &indCargo);
    if (!SQL_SUCCEEDED(resultado)) goto erro_autenticacao;
    SQLFreeHandle(SQL_HANDLE_STMT, consulta);
    consulta = SQL_NULL_HSTMT;

    if (!ativo || indSalt == SQL_NULL_DATA || indHash == SQL_NULL_DATA ||
        indSalt != TAMANHO_SALT || indHash != TAMANHO_HASH ||
        !derivarHashSenha(senha, salt, hashTentativa) ||
        !compararConstante(hashTentativa, hashBanco, TAMANHO_HASH)) {
        mg_free(matricula); mg_free(senha);
        responderErroLogin(c);
        return;
    }

    char nomeUtf8[301] = {0};
    char cargoUtf8[201] = {0};
    if (!converterWideUtf8(nomeW, nomeUtf8, sizeof(nomeUtf8)) ||
        !converterWideUtf8(cargoW, cargoUtf8, sizeof(cargoUtf8))) {
        mg_free(matricula); mg_free(senha);
        mg_http_reply(c, 500, "Content-Type: application/json; charset=utf-8\r\n",
            "{\"erro\":\"Não foi possível ler o perfil do funcionário.\"}\n");
        return;
    }
    SessaoSGE *sessao = criarSessao(funcionarioId, matricula, nomeUtf8, cargoUtf8, deveTrocar != 0);
    mg_free(matricula); mg_free(senha);
    if (sessao == NULL) {
        mg_http_reply(c, 503, "Content-Type: application/json; charset=utf-8\r\n",
            "{\"erro\":\"Limite de sessões atingido. Aguarde ou reinicie o servidor.\"}\n");
        return;
    }

    char json[1024] = {0};
    size_t posicao = 0;
    int valido = adicionarTexto(json, sizeof(json), &posicao, "{\"autenticado\":true,\"matricula\":");
    if (valido) valido = adicionarJson(json, sizeof(json), &posicao, sessao->matricula);
    if (valido) valido = adicionarTexto(json, sizeof(json), &posicao, ",\"nome\":");
    if (valido) valido = adicionarJson(json, sizeof(json), &posicao, sessao->nome);
    if (valido) valido = adicionarTexto(json, sizeof(json), &posicao, ",\"cargo\":");
    if (valido) valido = adicionarJson(json, sizeof(json), &posicao, sessao->cargo);
    if (valido) valido = adicionarTexto(json, sizeof(json), &posicao,
        sessao->senhaDeveTrocar ? ",\"trocarSenha\":true}" : ",\"trocarSenha\":false}");
    if (!valido) {
        memset(sessao, 0, sizeof(*sessao));
        mg_http_reply(c, 500, "Content-Type: application/json; charset=utf-8\r\n",
            "{\"erro\":\"Não foi possível montar a resposta do login.\"}\n");
        return;
    }
    char cabecalhos[256];
    snprintf(cabecalhos, sizeof(cabecalhos),
        "Content-Type: application/json; charset=utf-8\r\n"
        "Cache-Control: no-store\r\n"
        "Set-Cookie: sge_session=%s; HttpOnly; SameSite=Strict; Path=/; Max-Age=%d\r\n",
        sessao->token, DURACAO_SESSAO_SEGUNDOS);
    mg_http_reply(c, 200, cabecalhos, "%s\n", json);
    return;

erro_autenticacao:
    if (consulta != SQL_NULL_HSTMT) {
        mostrarErro(SQL_HANDLE_STMT, consulta);
        SQLFreeHandle(SQL_HANDLE_STMT, consulta);
    }
    if (matricula != NULL) mg_free(matricula);
    if (senha != NULL) mg_free(senha);
    mg_http_reply(c, 500, "Content-Type: application/json; charset=utf-8\r\n",
        "{\"erro\":\"Falha ao validar as credenciais no banco de dados.\"}\n");
}

static void obterUsuarioAtual(struct mg_connection *c, SessaoSGE *sessao) {
    char json[1024] = {0};
    size_t posicao = 0;
    int valido = adicionarTexto(json, sizeof(json), &posicao, "{\"autenticado\":true,\"matricula\":");
    if (valido) valido = adicionarJson(json, sizeof(json), &posicao, sessao->matricula);
    if (valido) valido = adicionarTexto(json, sizeof(json), &posicao, ",\"nome\":");
    if (valido) valido = adicionarJson(json, sizeof(json), &posicao, sessao->nome);
    if (valido) valido = adicionarTexto(json, sizeof(json), &posicao, ",\"cargo\":");
    if (valido) valido = adicionarJson(json, sizeof(json), &posicao, sessao->cargo);
    if (valido) valido = adicionarTexto(json, sizeof(json), &posicao,
        sessao->senhaDeveTrocar ? ",\"trocarSenha\":true}" : ",\"trocarSenha\":false}");
    if (!valido) {
        mg_http_reply(c, 500, "Content-Type: application/json; charset=utf-8\r\n", "{}\n");
        return;
    }
    mg_http_reply(c, 200, "Content-Type: application/json; charset=utf-8\r\nCache-Control: no-store\r\n", "%s\n", json);
}

static void encerrarSessao(struct mg_connection *c, struct mg_http_message *requisicao) {
    SessaoSGE *sessao = obterSessao(requisicao);
    if (sessao != NULL) memset(sessao, 0, sizeof(*sessao));
    mg_http_reply(c, 200,
        "Content-Type: application/json; charset=utf-8\r\n"
        "Set-Cookie: sge_session=; HttpOnly; SameSite=Strict; Path=/; Max-Age=0\r\n"
        "Cache-Control: no-store\r\n",
        "{\"mensagem\":\"Sessão encerrada.\"}\n");
}

static int validarSenhaFuncionario(SQLINTEGER id, const char *senha) {
    SQLHSTMT consulta = SQL_NULL_HSTMT;
    SQLRETURN resultado;
    unsigned char salt[TAMANHO_SALT] = {0}, hashBanco[TAMANHO_HASH] = {0}, tentativa[TAMANHO_HASH] = {0};
    SQLLEN indSalt = 0, indHash = 0;
    SQLCHAR sql[] = "SELECT SenhaSalt, SenhaHash FROM dbo.Funcionarios WHERE Id = ? AND Ativo = 1";
    resultado = SQLAllocHandle(SQL_HANDLE_STMT, conexao, &consulta);
    if (!SQL_SUCCEEDED(resultado)) return 0;
    resultado = SQLPrepare(consulta, sql, SQL_NTS);
    if (!SQL_SUCCEEDED(resultado)) goto erro_validar_senha;
    SQLLEN indId = 0;
    resultado = SQLBindParameter(consulta, 1, SQL_PARAM_INPUT, SQL_C_SLONG, SQL_INTEGER,
        0, 0, &id, sizeof(id), &indId);
    if (!SQL_SUCCEEDED(resultado)) goto erro_validar_senha;
    resultado = SQLExecute(consulta);
    if (!SQL_SUCCEEDED(resultado)) goto erro_validar_senha;
    resultado = SQLFetch(consulta);
    if (!SQL_SUCCEEDED(resultado)) { SQLFreeHandle(SQL_HANDLE_STMT, consulta); return 0; }
    resultado = SQLGetData(consulta, 1, SQL_C_BINARY, salt, sizeof(salt), &indSalt);
    if (!SQL_SUCCEEDED(resultado)) goto erro_validar_senha;
    resultado = SQLGetData(consulta, 2, SQL_C_BINARY, hashBanco, sizeof(hashBanco), &indHash);
    if (!SQL_SUCCEEDED(resultado)) goto erro_validar_senha;
    SQLFreeHandle(SQL_HANDLE_STMT, consulta);
    if (indSalt != TAMANHO_SALT || indHash != TAMANHO_HASH) return 0;
    return derivarHashSenha(senha, salt, tentativa) && compararConstante(tentativa, hashBanco, TAMANHO_HASH);

erro_validar_senha:
    mostrarErro(SQL_HANDLE_STMT, consulta);
    SQLFreeHandle(SQL_HANDLE_STMT, consulta);
    return 0;
}

static void trocarSenhaFuncionario(struct mg_connection *c,
                                   struct mg_http_message *requisicao,
                                   SessaoSGE *sessao) {
    struct mg_str corpo = requisicao->body;
    char *senhaAtual = mg_json_get_str(corpo, "$.senhaAtual");
    char *novaSenha = mg_json_get_str(corpo, "$.novaSenha");
    if (senhaAtual == NULL || novaSenha == NULL || strlen(novaSenha) < 10 || strlen(novaSenha) > 128) {
        if (senhaAtual != NULL) mg_free(senhaAtual);
        if (novaSenha != NULL) mg_free(novaSenha);
        mg_http_reply(c, 400, "Content-Type: application/json; charset=utf-8\r\n",
            "{\"erro\":\"Informe a senha atual e uma nova senha com pelo menos 10 caracteres.\"}\n");
        return;
    }
    if (!validarSenhaFuncionario(sessao->funcionarioId, senhaAtual)) {
        mg_free(senhaAtual); mg_free(novaSenha);
        mg_http_reply(c, 401, "Content-Type: application/json; charset=utf-8\r\n",
            "{\"erro\":\"A senha atual está incorreta.\"}\n");
        return;
    }
    if (strcmp(senhaAtual, novaSenha) == 0) {
        mg_free(senhaAtual); mg_free(novaSenha);
        mg_http_reply(c, 400, "Content-Type: application/json; charset=utf-8\r\n",
            "{\"erro\":\"A nova senha deve ser diferente da senha atual.\"}\n");
        return;
    }
    mg_free(senhaAtual);
    unsigned char salt[TAMANHO_SALT], hash[TAMANHO_HASH];
    if (!gerarSaltSenha(salt) || !derivarHashSenha(novaSenha, salt, hash)) {
        mg_free(novaSenha);
        mg_http_reply(c, 500, "Content-Type: application/json; charset=utf-8\r\n",
            "{\"erro\":\"Não foi possível proteger a nova senha.\"}\n");
        return;
    }
    mg_free(novaSenha);

    SQLHSTMT consulta = SQL_NULL_HSTMT;
    SQLRETURN resultado = SQLAllocHandle(SQL_HANDLE_STMT, conexao, &consulta);
    if (!SQL_SUCCEEDED(resultado)) {
        mg_http_reply(c, 500, "Content-Type: application/json; charset=utf-8\r\n",
            "{\"erro\":\"Não foi possível preparar a troca de senha.\"}\n");
        return;
    }
    SQLCHAR sql[] = "UPDATE dbo.Funcionarios SET SenhaSalt=?, SenhaHash=?, SenhaDeveTrocar=0 WHERE Id=? AND Ativo=1";
    resultado = SQLPrepare(consulta, sql, SQL_NTS);
    if (!SQL_SUCCEEDED(resultado)) goto erro_trocar_senha;
    SQLLEN indSalt = TAMANHO_SALT;
    resultado = SQLBindParameter(consulta, 1, SQL_PARAM_INPUT, SQL_C_BINARY, SQL_VARBINARY,
        TAMANHO_SALT, 0, salt, sizeof(salt), &indSalt);
    if (!SQL_SUCCEEDED(resultado)) goto erro_trocar_senha;
    SQLLEN indHash = TAMANHO_HASH;
    resultado = SQLBindParameter(consulta, 2, SQL_PARAM_INPUT, SQL_C_BINARY, SQL_VARBINARY,
        TAMANHO_HASH, 0, hash, sizeof(hash), &indHash);
    if (!SQL_SUCCEEDED(resultado)) goto erro_trocar_senha;
    SQLLEN indId = 0;
    resultado = SQLBindParameter(consulta, 3, SQL_PARAM_INPUT, SQL_C_SLONG, SQL_INTEGER,
        0, 0, &sessao->funcionarioId, sizeof(sessao->funcionarioId), &indId);
    if (!SQL_SUCCEEDED(resultado)) goto erro_trocar_senha;
    resultado = SQLExecute(consulta);
    if (!SQL_SUCCEEDED(resultado)) goto erro_trocar_senha;
    SQLFreeHandle(SQL_HANDLE_STMT, consulta);
    sessao->senhaDeveTrocar = false;
    mg_http_reply(c, 200, "Content-Type: application/json; charset=utf-8\r\n",
        "{\"mensagem\":\"Senha alterada com sucesso.\"}\n");
    return;

erro_trocar_senha:
    mostrarErro(SQL_HANDLE_STMT, consulta);
    SQLFreeHandle(SQL_HANDLE_STMT, consulta);
    mg_http_reply(c, 500, "Content-Type: application/json; charset=utf-8\r\n",
        "{\"erro\":\"Não foi possível salvar a nova senha.\"}\n");
}

static void tratarRequisicao(
    struct mg_connection *c,
    int evento,
    void *dados
) {
    if (evento != MG_EV_HTTP_MSG) {
        return;
    }

    struct mg_http_message *requisicao = dados;

    if (mg_match(requisicao->uri, mg_str("/api/login"), NULL)) {
        if (mg_match(requisicao->method, mg_str("POST"), NULL)) {
            autenticarUsuario(c, requisicao);
        } else {
            mg_http_reply(c, 405, "Content-Type: application/json; charset=utf-8\r\n",
                "{\"erro\":\"Método não permitido.\"}\n");
        }
        return;
    }
    if (mg_match(requisicao->uri, mg_str("/api/logout"), NULL)) {
        if (mg_match(requisicao->method, mg_str("POST"), NULL)) encerrarSessao(c, requisicao);
        else mg_http_reply(c, 405, "Content-Type: application/json; charset=utf-8\r\n", "{}\n");
        return;
    }
    if (mg_match(requisicao->uri, mg_str("/api/me"), NULL)) {
        SessaoSGE *sessaoAtual = obterSessao(requisicao);
        if (sessaoAtual == NULL) responderNaoAutenticado(c);
        else obterUsuarioAtual(c, sessaoAtual);
        return;
    }

    int requisicaoApi = requisicao->uri.len >= 5 &&
        memcmp(requisicao->uri.buf, "/api/", 5) == 0;
    SessaoSGE *sessaoAtual = NULL;
    if (requisicaoApi) {
        sessaoAtual = obterSessao(requisicao);
        if (sessaoAtual == NULL) {
            responderNaoAutenticado(c);
            return;
        }
        if (sessaoAtual->senhaDeveTrocar &&
            !mg_match(requisicao->uri, mg_str("/api/senha"), NULL)) {
            mg_http_reply(c, 403, "Content-Type: application/json; charset=utf-8\r\n",
                "{\"erro\":\"Troque a senha inicial antes de utilizar o sistema.\"}\n");
            return;
        }
        if (mg_match(requisicao->uri, mg_str("/api/senha"), NULL)) {
            if (mg_match(requisicao->method, mg_str("PUT"), NULL))
                trocarSenhaFuncionario(c, requisicao, sessaoAtual);
            else mg_http_reply(c, 405, "Content-Type: application/json; charset=utf-8\r\n", "{}\n");
            return;
        }
    }

    if (
        mg_match(requisicao->uri, mg_str("/api/hello"), NULL) &&
        mg_match(requisicao->method, mg_str("GET"), NULL)
    ) {
        mg_http_reply(
            c, 200, "Content-Type: application/json; charset=utf-8\r\n",
            "{\"status\":\"ok\","
            "\"mensagem\":\"Servidor C funcionando!\"}\n"
        );
    } else if (
        mg_match(requisicao->uri, mg_str("/api/funcionarios"), NULL)
    ) {
        if (mg_match(requisicao->method, mg_str("GET"), NULL)) {
            /* Todos os funcionários autenticados podem ler a lista para atribuir responsáveis. */
            listarFuncionarios(c);
        } else if (mg_match(requisicao->method, mg_str("POST"), NULL)) {
            if (!podeGerenciarFuncionarios(sessaoAtual)) responderAcessoNegado(c);
            else cadastrarFuncionario(c, requisicao);
        } else if (mg_match(requisicao->method, mg_str("PUT"), NULL)) {
            if (!podeGerenciarFuncionarios(sessaoAtual)) responderAcessoNegado(c);
            else atualizarFuncionario(c, requisicao);
        } else {
            mg_http_reply(
                c, 405, "Content-Type: application/json; charset=utf-8\r\n",
                "{\"erro\":\"Método não permitido.\"}\n"
            );
        }
    } else if (
        mg_match(requisicao->uri, mg_str("/api/encomendas/historico"), NULL
    )) {
        if (mg_match(requisicao->method, mg_str("GET"), NULL)) {
            listarHistoricoEncomenda(c, requisicao);
        } else {
            mg_http_reply(
                c, 405, "Content-Type: application/json; charset=utf-8\r\n",
                "{\"erro\":\"Método não permitido.\"}\n"
            );
        }
    } else if (mg_match(
        requisicao->uri, mg_str("/api/encomendas/status"), NULL
    )) {
        if (mg_match(requisicao->method, mg_str("PUT"), NULL)) {
            atualizarStatusEncomenda(c, requisicao, sessaoAtual);
        } else {
            mg_http_reply(
                c, 405, "Content-Type: application/json; charset=utf-8\r\n",
                "{\"erro\":\"Método não permitido.\"}\n"
            );
        }
    } else if (
        mg_match(requisicao->uri, mg_str("/api/encomendas"), NULL) &&
        mg_match(requisicao->method, mg_str("GET"), NULL)
    ) {
        listarEncomendas(c);
    } else if (
        mg_match(requisicao->uri, mg_str("/api/encomendas"), NULL) &&
        mg_match(requisicao->method, mg_str("POST"), NULL)
    ) {
        if (!podeCadastrarEncomendas(sessaoAtual)) responderAcessoNegado(c);
        else cadastrarEncomenda(c, requisicao);
    } else if (
        mg_match(requisicao->uri, mg_str("/api/encomendas"), NULL)
    ) {
        mg_http_reply(
            c, 405, "Content-Type: application/json; charset=utf-8\r\n",
            "{\"erro\":\"Método não permitido.\"}\n"
        );
    } else {
        struct mg_http_serve_opts opcoes = {
            .root_dir = "./frontend",
            .fs = &mg_fs_posix
        };

        mg_http_serve_dir(c, requisicao, &opcoes);
    }
}

int main(void) {
    struct mg_mgr servidor;

    if (!conectarBanco()) {
        printf("Nao foi possivel conectar ao banco.\n");
        goto finalizar;
    }

    mg_mgr_init(&servidor);

    if (mg_http_listen(
        &servidor, "http://127.0.0.1:8000",
        tratarRequisicao, NULL
    ) == NULL) {
        printf("Erro ao iniciar servidor HTTP.\n");
        mg_mgr_free(&servidor);
        goto finalizar;
    }

    printf("Servidor iniciado em http://127.0.0.1:8000\n");
    printf("API: http://127.0.0.1:8000/api/encomendas\n");
    printf("Pressione Ctrl+C para encerrar.\n");

    while (1) {
        mg_mgr_poll(&servidor, 1000);
    }

finalizar:
    if (conexao != SQL_NULL_HDBC) {
        SQLDisconnect(conexao);
        SQLFreeHandle(SQL_HANDLE_DBC, conexao);
    }

    if (ambiente != SQL_NULL_HENV) {
        SQLFreeHandle(SQL_HANDLE_ENV, ambiente);
    }

    return 1;
}