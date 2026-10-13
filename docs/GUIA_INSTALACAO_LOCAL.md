# Instalação e execução local do SGE

Este guia descreve como iniciar o protótipo no computador de desenvolvimento. Para executar em outro computador, também é preciso instalar e configurar o SQL Server e ajustar a conexão definida em `backend/src/server.c`.

## Requisitos
- Windows.
- MSYS2 UCRT64 com GCC em `C:\\msys64\\ucrt64\\bin\\gcc.exe`.
- Microsoft SQL Server com o banco `SGE_Encomendas` e as tabelas do projeto.
- ODBC Driver 18 for SQL Server.

## Compilar e iniciar
1. Abra a pasta principal do projeto.
2. Feche o servidor caso ele já esteja rodando (na janela dele, pressione `Ctrl+C`).
3. Execute `compilar_sge_corrigido.bat`.
4. Continue somente se aparecer `Codigo de saida: 0` e a confirmação de que `build\\sge_server.exe` foi criado.
5. Execute `executar_sge.bat` e mantenha a janela aberta.
6. No navegador, abra `http://127.0.0.1:8000`.
7. Para parar o servidor, volte à janela de execução e pressione `Ctrl+C`.

## Banco de dados
O servidor atual usa ODBC e tem o nome local da instância definido em `backend/src/server.c` (`SERVER=GlauconXP;`). Em outro computador, altere essa configuração para a instância SQL Server instalada lá. Antes de fazê-lo, mantenha uma cópia de segurança do arquivo.

Se o banco ainda não existir, crie/restaure `SGE_Encomendas` e configure as tabelas necessárias com os scripts SQL de `docs/`. Não execute scripts de criação ou configuração indiscriminadamente em um banco com dados importantes. Antes da apresentação, use um backup e teste todos os fluxos no notebook de destino.

## Limites de segurança
O protótipo foi preparado para execução local em `127.0.0.1:8000`, usa HTTP e contém opções de conexão adequadas apenas ao desenvolvimento. Não exponha a porta à internet nem utilize o projeto para dados reais sensíveis.
