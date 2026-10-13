# SGE — Sistema de Gestão de Encomendas

Protótipo acadêmico desenvolvido para o Projeto Integrado Multidisciplinar (PIM). O sistema simula o controle interno de encomendas de uma unidade de distribuição fictícia. Não é um sistema oficial dos Correios.

## O que o sistema faz

- Autentica funcionários por matrícula e senha, com troca obrigatória da senha temporária no primeiro acesso.
- Aplica permissões por cargo no servidor, além de controlar a visibilidade de opções na interface.
- Cadastra e consulta encomendas no SQL Server.
- Associa uma encomenda a um funcionário responsável.
- Avança o status respeitando a sequência definida e registra movimentações.
- Mantém cadastro de funcionários, com edição e ativação/inativação.
- Exibe indicadores do Dashboard e relatórios calculados a partir do banco.
- Permite busca, filtros, detalhes e consulta do histórico de encomendas.

## Tecnologias

| Componente | Tecnologia | Papel |
|---|---|---|
| Interface | HTML, CSS e JavaScript | Telas, formulários e chamadas à API |
| Servidor | C | Servidor HTTP, endpoints, validações e regras de negócio |
| Banco de dados | Microsoft SQL Server | Persistência de funcionários, encomendas e movimentações |
| Acesso ao banco | ODBC Driver 18 for SQL Server | Comunicação entre o servidor C e o SQL Server |
| Servidor HTTP | Mongoose | Recebe requisições HTTP e serve os arquivos da interface |
| Autenticação | Hash com salt e APIs criptográficas do Windows | Proteção das senhas armazenadas |

## Estrutura principal

```text
PIM_2/
├── backend/
│   ├── src/server.c                 # API, autenticação e regras de negócio
│   └── third_party/mongoose/        # Biblioteca HTTP usada pelo servidor
├── frontend/
│   ├── index.html                   # Estrutura da interface
│   ├── style.css                    # Estilos visuais
│   └── script.js                    # Interações e chamadas à API
├── docs/
│   ├── auth_setup.sql               # Colunas e credenciais temporárias de demonstração
│   ├── seed_funcionarios_demo.sql   # Dados de funcionários demonstrativos
│   ├── ARQUITETURA.md               # Visão técnica da solução
│   ├── ENDPOINTS_API.md             # Rotas e responsabilidades
│   ├── PLANO_TESTES_FINAL.md        # Lista de testes manuais
│   ├── GUIA_APRESENTACAO.md         # Roteiro de apresentação
│   └── ESTADO_E_LIMITACOES.md       # Limitações do protótipo
├── build/                           # Executável gerado ao compilar
├── compilar_sge_corrigido.bat       # Compila usando o MSYS2 UCRT64
├── executar_sge.bat                 # Inicia o servidor
└── README.md
```

> A estrutura pode incluir arquivos auxiliares e backups de etapas anteriores. O arquivo ativo do site é `frontend/script.js` e o servidor principal é `backend/src/server.c`.

## Requisitos locais

- Windows.
- MSYS2 com ambiente UCRT64 instalado em `C:\msys64\ucrt64`.
- SQL Server e banco `SGE_Encomendas`.
- ODBC Driver 18 for SQL Server.
- Os arquivos da biblioteca Mongoose em `backend/third_party/mongoose/`.

## Preparar e iniciar

1. Abra o SQL Server Management Studio (SSMS) e confira se o banco `SGE_Encomendas` e as tabelas `Funcionarios`, `Encomendas` e `Movimentacoes` existem.
2. Se as colunas de autenticação ainda não foram preparadas, abra `docs/auth_setup.sql` no SSMS e execute o script no banco correto. O script é para a conta de demonstração e não deve substituir credenciais já configuradas.
3. Abra a pasta principal do projeto no Explorador de Arquivos.
4. Execute `compilar_sge_corrigido.bat`. Só continue quando mostrar código de saída `0` e `SUCESSO`.
5. Execute `executar_sge.bat` e mantenha essa janela aberta enquanto usar o sistema.
6. Abra `http://127.0.0.1:8000` no navegador. Use `Ctrl + F5` após substituir arquivos da interface.
7. Para parar o servidor, volte à janela de execução e pressione `Ctrl + C`.

Se o executável estiver aberto, feche o servidor antes de recompilar, pois o Windows pode bloquear a substituição de `build/sge_server.exe`.

## Banco de dados

O servidor C usa ODBC para conectar ao banco `SGE_Encomendas`. A configuração local está no código de `backend/src/server.c`; se o nome do servidor SQL for diferente em outro computador, a string de conexão deve ser ajustada.

Tabelas principais:

- `Funcionarios`: matrícula, nome, cargo, setor, estado ativo/inativo e credenciais de autenticação.
- `Encomendas`: código, destinatário, endereço, status, prioridade, datas e funcionário responsável.
- `Movimentacoes`: alterações de status e informações associadas ao histórico.

## Papéis e permissões

As regras por cargo são aplicadas na API, não apenas na interface. Consulte [`docs/PERMISSOES_CARGOS.md`](docs/PERMISSOES_CARGOS.md) para a matriz de permissões. Em resumo, a supervisão gerencia funcionários e relatórios; os demais cargos recebem as operações necessárias às suas responsabilidades.

## Documentação útil

- [`docs/ARQUITETURA.md`](docs/ARQUITETURA.md): como as partes conversam.
- [`docs/ENDPOINTS_API.md`](docs/ENDPOINTS_API.md): visão geral das rotas.
- [`docs/PLANO_TESTES_FINAL.md`](docs/PLANO_TESTES_FINAL.md): checklist para a validação final.
- [`docs/GUIA_APRESENTACAO.md`](docs/GUIA_APRESENTACAO.md): roteiro inicial para apresentar o sistema.
- [`docs/ESTADO_E_LIMITACOES.md`](docs/ESTADO_E_LIMITACOES.md): escopo e limitações conhecidas.

## Segurança e escopo

O SGE é um protótipo acadêmico executado localmente. Ele escuta em `127.0.0.1:8000`; não exponha essa porta diretamente à rede ou à internet. A configuração local inclui `TrustServerCertificate=Yes`, adequada apenas para desenvolvimento. Antes de uso real, seria necessária uma revisão de segurança, HTTPS/TLS, limitação de tentativas, gestão segura de segredos, auditoria e testes profissionais.

A biblioteca Mongoose mantém os avisos de licença em seus próprios arquivos. Consulte os termos aplicáveis antes de distribuir publicamente o código.
