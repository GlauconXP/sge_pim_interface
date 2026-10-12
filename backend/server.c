#include "mongoose.h"
#include <windows.h>
#include <sql.h>
#include <sqlext.h>
#include <stdio.h>
#include <string.h>

static SQLHENV ambiente = SQL_NULL_HENV;
static SQLHDBC conexao = SQL_NULL_HDBC;

static void mostrarErro(SQLSMALLINT tipo, SQLHANDLE handle) {
    SQLCHAR estado[6] = {0};
    SQLCHAR mensagem[1024] = {0};
    SQLINTEGER codigo;
    SQLSMALLINT tamanho;

    if (SQL_SUCCEEDED(SQLGetDiagRec(
        tipo, handle, 1, estado, &codigo,
        mensagem, sizeof(mensagem), &tamanho
    ))) {
        printf("Erro ODBC: %s\n", mensagem);
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

static int lerColuna(
    SQLHSTMT consulta, SQLUSMALLINT coluna,
    SQLCHAR *buffer, SQLLEN capacidade
) {
    SQLLEN tamanho;
    SQLRETURN resultado = SQLGetData(
        consulta, coluna, SQL_C_CHAR,
        buffer, capacidade, &tamanho
    );

    if (!SQL_SUCCEEDED(resultado)) {
        mostrarErro(SQL_HANDLE_STMT, consulta);
        return 0;
    }

    if (tamanho == SQL_NULL_DATA) {
        buffer[0] = '\0';
    } else {
        buffer[capacidade - 1] = '\0';
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

    SQLWCHAR codigo[64];
    SQLWCHAR destinatario[201];
    SQLWCHAR endereco[401];
    SQLWCHAR cep[20];
    SQLWCHAR bairro[161];
    SQLWCHAR cidade[161];
    SQLWCHAR tipo[61];
    SQLWCHAR status[61];
    SQLWCHAR prioridade[41];

    struct mg_str corpo = requisicao->body;

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

    if (!valido) {
        mg_http_reply(
            c, 400,
            "Content-Type: application/json\r\n",
            "{\"erro\":\"Preencha corretamente todos os campos.\"}\n"
        );
        return;
    }

    if (!SQL_SUCCEEDED(SQLAllocHandle(
        SQL_HANDLE_STMT, conexao, &consulta
    ))) {
        mg_http_reply(
            c, 500,
            "Content-Type: application/json\r\n",
            "{\"erro\":\"Falha ao preparar o cadastro.\"}\n"
        );
        return;
    }

    SQLCHAR sql[] =
        "INSERT INTO dbo.Encomendas "
        "(Codigo, Destinatario, Endereco, CEP, Bairro, "
        "Cidade, TipoEnvio, Status, Prioridade) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)";

    SQLRETURN resultado = SQLPrepare(
        consulta, sql, SQL_NTS
    );

    if (!SQL_SUCCEEDED(resultado)) {
        goto erro_sql;
    }

    SQLWCHAR *valores[] = {
        codigo, destinatario, endereco, cep, bairro,
        cidade, tipo, status, prioridade
    };

    SQLULEN tamanhosColuna[] = {
        30, 100, 200, 9, 80, 80, 30, 30, 20
    };

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
                (i == 0 ? sizeof(codigo) :
                 i == 1 ? sizeof(destinatario) :
                 i == 2 ? sizeof(endereco) :
                 i == 3 ? sizeof(cep) :
                 i == 4 ? sizeof(bairro) :
                 i == 5 ? sizeof(cidade) :
                 i == 6 ? sizeof(tipo) :
                 i == 7 ? sizeof(status) :
                          sizeof(prioridade))
            ),
            &comprimentos[i]
        );

        if (!SQL_SUCCEEDED(resultado)) {
            goto erro_sql;
        }
    }

    resultado = SQLExecute(consulta);

    if (!SQL_SUCCEEDED(resultado)) {
        goto erro_sql;
    }

    SQLFreeHandle(SQL_HANDLE_STMT, consulta);

    mg_http_reply(
        c, 201,
        "Content-Type: application/json; charset=utf-8\r\n",
        "{\"mensagem\":\"Encomenda cadastrada com sucesso.\"}\n"
    );

    return;

erro_sql:
    mostrarErro(SQL_HANDLE_STMT, consulta);

    SQLFreeHandle(SQL_HANDLE_STMT, consulta);

    mg_http_reply(
        c, 400,
        "Content-Type: application/json; charset=utf-8\r\n",
        "{\"erro\":\"Cadastro nao realizado. Verifique se o codigo "
        "ja existe e se os dados sao validos.\"}\n"
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
        "SELECT TOP (100) Codigo, Destinatario, Cidade, Status "
        "FROM dbo.Encomendas ORDER BY Id DESC";

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
                consulta, 4, status, sizeof(status))) {
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

static void tratarRequisicao(
    struct mg_connection *c,
    int evento,
    void *dados
) {
    if (evento != MG_EV_HTTP_MSG) {
        return;
    }

    struct mg_http_message *requisicao = dados;

    if (mg_match(
        requisicao->uri, mg_str("/api/hello"), NULL
    )) {
        mg_http_reply(
            c, 200, "Content-Type: application/json; charset=utf-8\r\n",
            "{\"status\":\"ok\","
            "\"mensagem\":\"Servidor C funcionando!\"}\n"
        );
    } else if (
        mg_match(requisicao->uri, mg_str("/api/encomendas"), NULL) &&
        mg_match(requisicao->method, mg_str("GET"), NULL)
    ) {
        listarEncomendas(c);
    } else if (
        mg_match(requisicao->uri, mg_str("/api/encomendas"), NULL) &&
        mg_match(requisicao->method, mg_str("POST"), NULL)
    ) {
        cadastrarEncomenda(c, requisicao);
    } else if (
        mg_match(requisicao->uri, mg_str("/api/encomendas"), NULL)
    ) {
        mg_http_reply(
            c, 405, "Content-Type: application/json; charset=utf-8\r\n",
            "{\"erro\":\"Metodo nao permitido.\"}\n"
        );
    } else {
        struct mg_http_serve_opts opcoes = {
            .root_dir = ".",
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