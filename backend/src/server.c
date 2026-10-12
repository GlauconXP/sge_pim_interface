#include "mongoose.h"
#include <windows.h>
#include <sql.h>
#include <sqlext.h>
#include <stdio.h>
#include <string.h>
#include <limits.h>

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

    if (!valido || codigo[0] == 0) {
        mg_http_reply(
            c, 400, "Content-Type: application/json; charset=utf-8\r\n",
            "{\"erro\":\"Preencha corretamente todos os campos.\"}\n"
        );
        return;
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
            "Cidade, TipoEnvio, Status, Prioridade) "
            "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)";
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


/* Valida os status que o sistema permite registrar. */
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
    struct mg_http_message *requisicao
) {
    SQLHSTMT consulta = SQL_NULL_HSTMT;
    SQLRETURN resultado;
    SQLLEN linhasAfetadas = -1;
    int transacaoAtiva = 0;
    const char *etapa = "validar os dados da requisição";
    SQLWCHAR codigo[64] = {0};
    SQLWCHAR status[61] = {0};
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

static void tratarRequisicao(
    struct mg_connection *c,
    int evento,
    void *dados
) {
    if (evento != MG_EV_HTTP_MSG) {
        return;
    }

    struct mg_http_message *requisicao = dados;

    if (
        mg_match(requisicao->uri, mg_str("/api/hello"), NULL) &&
        mg_match(requisicao->method, mg_str("GET"), NULL)
    ) {
        mg_http_reply(
            c, 200, "Content-Type: application/json; charset=utf-8\r\n",
            "{\"status\":\"ok\","
            "\"mensagem\":\"Servidor C funcionando!\"}\n"
        );
    } else if (mg_match(
        requisicao->uri, mg_str("/api/encomendas/historico"), NULL
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
            atualizarStatusEncomenda(c, requisicao);
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
        cadastrarEncomenda(c, requisicao);
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