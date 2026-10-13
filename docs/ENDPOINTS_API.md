# Endpoints da API do SGE

Esta lista descreve a interface HTTP do protótipo. Todas as rotas `/api/...`, exceto o login e as operações explicitamente públicas, exigem sessão válida. Uma sessão com senha temporária pendente deve trocar a senha antes de utilizar as demais funções.

| Método | Rota | Finalidade | Acesso geral |
|---|---|---|---|
| `POST` | `/api/login` | Validar matrícula e senha e iniciar sessão | Público; as credenciais são validadas no servidor |
| `POST` | `/api/logout` | Encerrar a sessão atual | Sessão atual |
| `GET` | `/api/me` | Consultar o funcionário associado à sessão | Sessão atual |
| `PUT` | `/api/senha` | Trocar senha inicial ou redefinir senha conforme o fluxo implementado | Sessão atual e validações específicas |
| `GET` | `/api/hello` | Verificar se a API está respondendo | Requer sessão no roteamento atual |
| `GET` | `/api/encomendas` | Listar as encomendas recentes | Funcionário autenticado |
| `POST` | `/api/encomendas` | Cadastrar uma encomenda | Atendente e supervisão/administração, de acordo com a matriz de cargos |
| `GET` | `/api/encomendas/detalhe?codigo=...` | Consultar detalhes por código | Funcionário autenticado |
| `GET` | `/api/encomendas/historico?codigo=...` | Consultar as movimentações de uma encomenda | Funcionário autenticado |
| `PUT` | `/api/encomendas/status` | Avançar o status e registrar movimentação | Supervisão, conferente e entregador, respeitando o fluxo |
| `GET` | `/api/funcionarios` | Listar funcionários para a interface | Funcionário autenticado |
| `POST` | `/api/funcionarios` | Cadastrar funcionário | Supervisão/administrador |
| `PUT` | `/api/funcionarios` | Editar funcionário, alterar estado ou dados permitidos | Supervisão/administrador |
| `GET` | `/api/dashboard` | Obter indicadores do Dashboard | Funcionário autenticado |
| `GET` | `/api/relatorios` | Obter indicadores para relatórios | Apenas cargos autorizados no servidor |

## Notas sobre as respostas

- A API usa JSON para transportar dados e mensagens de erro.
- Códigos HTTP como `401`, `403`, `404`, `405` e `500` indicam, respectivamente, falta de autenticação, falta de permissão, registro não encontrado, método não aceito e erro interno.
- Os detalhes e o histórico são consultados pelo código da encomenda; não dependem apenas da linha que está visível na tabela da página.
- Os nomes exatos dos campos JSON são definidos no `frontend/script.js` e no servidor C. Consulte os arquivos ao alterar o contrato da API.

## Teste manual da API

O teste recomendado para o usuário final é pelo navegador e pela interface. Para depuração, abra `http://127.0.0.1:8000/api/me`: sem uma sessão válida, a resposta JSON de sessão inválida é o resultado esperado; não deve ser interpretado como falha de rota.
