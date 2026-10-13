# Estado atual e limitações do protótipo

## Escopo demonstrado durante o desenvolvimento

O protótipo foi desenvolvido localmente e testado em etapas, incluindo cadastro e consulta de encomendas, atualização de status e histórico, vínculo com funcionário responsável, gestão de funcionários, autenticação por matrícula, troca obrigatória de senha inicial, permissões por cargo, Dashboard e relatórios baseados nos dados do SQL Server.

Este documento não substitui a execução do plano de testes final. Antes da apresentação, marque os testes que foram repetidos na versão/commit que será demonstrada.

## Indicadores e definições

- **Recebidas, Em triagem, Separada, Em distribuição e Entregue** são estados atuais de encomendas.
- **Taxa de entrega** significa a porcentagem de encomendas cadastradas cujo estado atual é `Entregue`; não é, por si só, um indicador histórico por período.
- **Pendentes** são encomendas cujo estado ainda não é `Entregue`. O protótipo não deve chamar esse total de “atrasadas”, pois o esquema não contém prazo previsto de entrega.
- **Tempo médio em triagem** depende de movimentações registradas suficientes para identificar os eventos considerados. Registros antigos podem não conter histórico completo.

## Limitações conhecidas

- O servidor escuta somente em `127.0.0.1:8000` e usa HTTP. O protótipo não foi preparado para exposição direta na rede ou internet.
- A string local do ODBC usa autenticação integrada e `TrustServerCertificate=Yes`, configuração de desenvolvimento que precisa de revisão antes de outro ambiente.
- As sessões são mantidas na memória do processo; reiniciar o servidor encerra as sessões.
- O protótipo precisa de limitação de tentativas de login, monitoramento e procedimentos de recuperação seguros antes de qualquer implantação real.
- A senha temporária de demonstração e os dados de teste não devem ser reutilizados fora do ambiente acadêmico. Cada conta de demonstração deve trocar a senha no primeiro acesso.
- O projeto não é um produto oficial dos Correios e não deve usar a identidade visual institucional como se fosse autorizado.
- A licença da biblioteca Mongoose deve ser revisada antes de redistribuir o software publicamente.

## Melhorias futuras possíveis

- Testes automatizados de unidade e integração.
- Auditoria de ações com identidade do funcionário que executou cada movimentação.
- Exportação de relatórios.
- Política de senhas e limitação de tentativas mais completas.
- HTTPS/TLS, configuração segura de segredos e revisão profissional antes de qualquer uso real.
- Migração/backup documentados do banco de dados.
