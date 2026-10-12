# Arquitetura do SGE

O projeto é dividido em três partes:

1. **Frontend (`frontend/`)**: HTML, CSS e JavaScript compõem as telas e enviam requisições HTTP.
2. **Servidor em C (`backend/src/server.c`)**: a biblioteca Mongoose recebe as requisições e o ODBC comunica o servidor C com o SQL Server.
3. **Banco de dados SQL Server**: armazena os registros de encomendas.

## Fluxo de consulta

`Navegador → GET /api/encomendas → servidor C → ODBC → SQL Server → JSON → navegador`

## Fluxo de cadastro

`Formulário → POST /api/encomendas → servidor C → ODBC parametrizado → SQL Server → confirmação HTTP → atualização da lista`

## Fluxo de atualização de status

`Botão Avançar → PUT /api/encomendas/status → transação ODBC → UPDATE em Encomendas + INSERT em Movimentacoes → confirmação HTTP`

## Fluxo de consulta do histórico

`Modal de encomenda → GET /api/encomendas/historico?codigo=... → servidor C → consulta a Movimentacoes → JSON → modal`

## Rotas implementadas

| Método | Caminho | Função |
|---|---|---|
| `GET` | `/api/encomendas` | Consulta as 100 encomendas mais recentes. |
| `POST` | `/api/encomendas` | Cadastra uma encomenda. |
| `PUT` | `/api/encomendas/status` | Atualiza o status e registra uma movimentação numa transação. |
| `GET` | `/api/encomendas/historico?codigo=...` | Consulta o histórico persistido de uma encomenda. |
| `GET` | `/api/hello` | Verifica se o servidor HTTP responde. |

Os arquivos em `docs/backup/` são apenas referências antigas e não fazem parte do carregamento normal do site.
