#include <windows.h>
#include <sql.h>
#include <sqlext.h>
#include <stdio.h>

void mostrarErro(SQLSMALLINT tipo, SQLHANDLE handle) {
    SQLCHAR estado[6];
    SQLCHAR mensagem[1024];
    SQLINTEGER codigo;
    SQLSMALLINT tamanho;

    if (SQL_SUCCEEDED(SQLGetDiagRec(
        tipo, handle, 1,
        estado, &codigo,
        mensagem, sizeof(mensagem), &tamanho
    ))) {
        printf("Erro ODBC: %s\n", mensagem);
        printf("SQLSTATE: %s\n", estado);
    }
}

int main(void) {
    SQLHENV ambiente = SQL_NULL_HENV;
    SQLHDBC conexao = SQL_NULL_HDBC;
    SQLHSTMT consulta = SQL_NULL_HSTMT;

    SQLRETURN resultado;
    int conectado = 0;
    int sucesso = 0;

    SQLCHAR banco[128] = {0};

    SQLCHAR stringConexao[] =
        "DRIVER={ODBC Driver 18 for SQL Server};"
        "SERVER=GlauconXP;"
        "DATABASE=SGE_Encomendas;"
        "Trusted_Connection=Yes;"
        "Encrypt=Yes;"
        "TrustServerCertificate=Yes;";

    printf("Conectando ao SQL Server...\n");

    resultado = SQLAllocHandle(
        SQL_HANDLE_ENV,
        SQL_NULL_HANDLE,
        &ambiente
    );

    if (!SQL_SUCCEEDED(resultado)) {
        printf("Erro ao criar o ambiente ODBC.\n");
        goto finalizar;
    }

    resultado = SQLSetEnvAttr(
        ambiente,
        SQL_ATTR_ODBC_VERSION,
        (SQLPOINTER)SQL_OV_ODBC3,
        0
    );

    if (!SQL_SUCCEEDED(resultado)) {
        mostrarErro(SQL_HANDLE_ENV, ambiente);
        goto finalizar;
    }

    resultado = SQLAllocHandle(
        SQL_HANDLE_DBC,
        ambiente,
        &conexao
    );

    if (!SQL_SUCCEEDED(resultado)) {
        printf("Erro ao criar a conexao ODBC.\n");
        goto finalizar;
    }

    resultado = SQLDriverConnect(
        conexao,
        NULL,
        stringConexao,
        SQL_NTS,
        NULL,
        0,
        NULL,
        SQL_DRIVER_NOPROMPT
    );

    if (!SQL_SUCCEEDED(resultado)) {
        mostrarErro(SQL_HANDLE_DBC, conexao);
        goto finalizar;
    }

    conectado = 1;
    printf("Conexao estabelecida!\n");

    resultado = SQLAllocHandle(
        SQL_HANDLE_STMT,
        conexao,
        &consulta
    );

    if (!SQL_SUCCEEDED(resultado)) {
        mostrarErro(SQL_HANDLE_DBC, conexao);
        goto finalizar;
    }
    const char *sqlInserir =
    "IF NOT EXISTS (SELECT 1 FROM dbo.Encomendas "
    "WHERE Codigo = 'PIM-TESTE-001') "
    "BEGIN "
    "INSERT INTO dbo.Encomendas "
    "(Codigo, Destinatario, Endereco, CEP, Bairro, "
    "Cidade, TipoEnvio, Status, Prioridade) "
    "VALUES "
    "('PIM-TESTE-001', 'Joao da Silva', "
    "'Rua Exemplo, 123', '01000-000', 'Centro', "
    "'Sao Paulo', 'SEDEX', 'Recebida', 'Normal'); "
    "END;";

resultado = SQLExecDirect(
    consulta,
    (SQLCHAR *)sqlInserir,
    SQL_NTS
);

if (!SQL_SUCCEEDED(resultado)) {
    mostrarErro(SQL_HANDLE_STMT, consulta);
    goto finalizar;
}

printf("Teste de insercao executado com sucesso!\n");

SQLFreeStmt(consulta, SQL_CLOSE);
    resultado = SQLExecDirect(
        consulta,
        (SQLCHAR *)"SELECT DB_NAME()",
        SQL_NTS
    );

    if (!SQL_SUCCEEDED(resultado)) {
        mostrarErro(SQL_HANDLE_STMT, consulta);
        goto finalizar;
    }

    resultado = SQLFetch(consulta);

    if (SQL_SUCCEEDED(resultado)) {
        resultado = SQLGetData(
            consulta,
            1,
            SQL_C_CHAR,
            banco,
            sizeof(banco),
            NULL
        );

        if (SQL_SUCCEEDED(resultado)) {
            printf("Banco conectado: %s\n", banco);
            sucesso = 1;
        } else {
            mostrarErro(SQL_HANDLE_STMT, consulta);
        }
    } else {
        mostrarErro(SQL_HANDLE_STMT, consulta);
    }
    {
    SQLCHAR codigo[64] = {0};
    SQLCHAR destinatario[128] = {0};
    SQLCHAR status[64] = {0};

    SQLLEN tamanhoCodigo;
    SQLLEN tamanhoDestinatario;
    SQLLEN tamanhoStatus;

    int total = 0;

    printf("\n=== ENCOMENDAS CADASTRADAS ===\n");

    SQLFreeStmt(consulta, SQL_CLOSE);

    resultado = SQLExecDirect(
        consulta,
        (SQLCHAR *)
        "SELECT Codigo, Destinatario, Status "
        "FROM dbo.Encomendas ORDER BY Id",
        SQL_NTS
    );

    if (!SQL_SUCCEEDED(resultado)) {
        mostrarErro(SQL_HANDLE_STMT, consulta);
        goto finalizar;
    }

    while ((resultado = SQLFetch(consulta)) != SQL_NO_DATA) {
        if (!SQL_SUCCEEDED(resultado)) {
            mostrarErro(SQL_HANDLE_STMT, consulta);
            goto finalizar;
        }

        resultado = SQLGetData(
            consulta, 1, SQL_C_CHAR,
            codigo, sizeof(codigo), &tamanhoCodigo
        );

        if (!SQL_SUCCEEDED(resultado)) {
            mostrarErro(SQL_HANDLE_STMT, consulta);
            goto finalizar;
        }

        resultado = SQLGetData(
            consulta, 2, SQL_C_CHAR,
            destinatario, sizeof(destinatario),
            &tamanhoDestinatario
        );

        if (!SQL_SUCCEEDED(resultado)) {
            mostrarErro(SQL_HANDLE_STMT, consulta);
            goto finalizar;
        }

        resultado = SQLGetData(
            consulta, 3, SQL_C_CHAR,
            status, sizeof(status), &tamanhoStatus
        );

        if (!SQL_SUCCEEDED(resultado)) {
            mostrarErro(SQL_HANDLE_STMT, consulta);
            goto finalizar;
        }

        printf("\nCodigo: %s\n", codigo);
        printf("Destinatario: %s\n", destinatario);
        printf("Status: %s\n", status);
        printf("-----------------------------\n");

        total++;
    }

    printf("\nTotal de encomendas: %d\n", total);
}
finalizar:
    if (consulta != SQL_NULL_HSTMT) {
        SQLFreeHandle(SQL_HANDLE_STMT, consulta);
    }

    if (conexao != SQL_NULL_HDBC) {
        if (conectado) {
            SQLDisconnect(conexao);
        }

        SQLFreeHandle(SQL_HANDLE_DBC, conexao);
    }

    if (ambiente != SQL_NULL_HENV) {
        SQLFreeHandle(SQL_HANDLE_ENV, ambiente);
    }

    if (sucesso) {
        printf("Teste concluido com sucesso!\n");
    }

    return sucesso ? 0 : 1;
}