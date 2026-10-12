# SGE — Sistema de Gestão de Encomendas

Protótipo acadêmico do PIM para registrar e consultar encomendas em uma unidade de distribuição fictícia.

## Organização das pastas

```text
sge_pim_interface/
├── frontend/                 # Interface exibida no navegador
│   ├── index.html             # Estrutura das telas
│   ├── style.css              # Aparência e layout
│   └── script.js              # Interações e chamadas para a API
├── backend/
│   ├── src/
│   │   └── server.c           # Servidor HTTP, API e acesso ao SQL Server
│   ├── examples/
│   │   └── teste_conexao_sqlserver.c  # Programa de teste separado
│   └── third_party/
│       └── mongoose/           # Biblioteca HTTP Mongoose
├── docs/
│   ├── ARQUITETURA.md          # Resumo da comunicação entre as partes
│   └── backup/
│       └── script_anterior.js  # Cópia antiga; não é carregada pelo site
├── build/                     # Executável gerado ao compilar (não versionado)
├── .gitignore
└── README.md
```

## O que é necessário

- Windows com GCC disponível no terminal, por exemplo pelo MSYS2 UCRT64.
- O Driver ODBC 18 for SQL Server.
- SQL Server com o banco `SGE_Encomendas` e as tabelas do projeto.
- GitHub Desktop ou Git, caso queira acompanhar as alterações.

## Compilar e iniciar

Abra o terminal do VS Code **na pasta principal do projeto**. Execute os comandos abaixo no PowerShell:

```powershell
New-Item -ItemType Directory -Force .\build | Out-Null
gcc -D_POSIX_C_SOURCE=200000L .\backend\src\server.c .\backend\third_party\mongoose\mongoose.c -I.\backend\third_party\mongoose -o .\build\sge_server.exe -lws2_32 -lodbc32
.\build\sge_server.exe
```

Com o servidor iniciado, abra `http://127.0.0.1:8000` no navegador. Não abra `frontend/index.html` diretamente pelo Explorador de Arquivos: a interface usa a API do servidor.

Para encerrar o servidor, pressione `Ctrl+C` no terminal.

## Banco de dados

A conexão está configurada em `backend/src/server.c` para o servidor local `GlauconXP` e banco `SGE_Encomendas`, usando autenticação integrada do Windows. Se o nome da instância for diferente em outra máquina, ajuste a string de conexão em `conectarBanco()`.

`TrustServerCertificate=Yes` está configurado para facilitar o desenvolvimento local. Não copie essa opção automaticamente para um ambiente de produção.

## Estado atual do protótipo

- A consulta (`GET /api/encomendas`) lê as encomendas do SQL Server.
- O cadastro (`POST /api/encomendas`) envia os dados do formulário para o servidor C e grava no SQL Server.
- A atualização de status (`PUT /api/encomendas/status`) atualiza `Encomendas.Status` e inclui uma linha em `Movimentacoes` na mesma transação.
- O histórico (`GET /api/encomendas/historico?codigo=...`) consulta as movimentações persistidas.
- A tela de login é demonstrativa; ainda não verifica credenciais reais.
- Funcionários responsáveis ainda são dados demonstrativos da interface e não são associados ao registro no banco.
- O modal de consulta lê as movimentações reais da API. Registros antigos podem não ter histórico se foram cadastrados antes desta funcionalidade.
- Alguns indicadores e relatórios são demonstrativos e ainda não são calculados a partir do banco.

## Biblioteca Mongoose

Os arquivos de `backend/third_party/mongoose/` mantêm os avisos de licença originais. A biblioteca indica licenciamento GPL-2.0-only ou licença comercial. Confira os termos aplicáveis antes de distribuir publicamente o projeto.
