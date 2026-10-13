# Permissões por cargo no SGE

As permissões são verificadas no servidor C. Esconder botões na interface melhora a experiência, mas não substitui as verificações da API.

| Cargo | Consultar encomendas | Cadastrar encomendas | Avançar status | Gerenciar funcionários | Relatórios |
|---|---:|---:|---:|---:|---:|
| Supervisora / Supervisor / Administrador | Sim | Sim | Sim, uma etapa por vez | Sim | Sim |
| Atendente | Sim | Sim | Não | Não | Não |
| Conferente | Sim | Não | Para Em triagem ou Separada, respeitando a próxima etapa | Não | Não |
| Entregador | Sim | Não | Para Em distribuição ou Entregue, respeitando a próxima etapa | Não | Não |
| Administrativo | Sim | Não | Não | Não | Sim |

Observações:
- `GET /api/funcionarios` continua disponível para usuários autenticados, pois o cadastro de encomendas usa a lista para escolher um responsável.
- `POST` e `PUT /api/funcionarios` exigem cargo de supervisão/administrador.
- `POST /api/encomendas` exige atendente ou supervisão/administrador.
- `PUT /api/encomendas/status` exige supervisão/administrador, conferente ou entregador. O servidor valida também que o status passe para a etapa imediatamente seguinte.
- As permissões são baseadas no texto do campo `Cargo` da tabela `dbo.Funcionarios`. Use os nomes dos cargos definidos na tabela.
