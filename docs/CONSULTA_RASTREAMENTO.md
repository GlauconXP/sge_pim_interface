# Consulta e rastreamento de encomendas

A lista apresenta até 500 encomendas mais recentes com código, destinatário, endereço, CEP, bairro, cidade, tipo de envio, status, prioridade, data de recebimento e responsável. A busca textual considera esses campos e pode ser combinada com filtros de status, tipo e prioridade.

O modal **Ver** consulta os detalhes completos por código na API `GET /api/encomendas/detalhe?codigo=...`, usando parâmetro SQL para procurar o registro. Em seguida, carrega o histórico persistido pela rota `GET /api/encomendas/historico?codigo=...`.

A ação **Consultar encomenda** solicita um código e consulta diretamente no banco, por isso não depende de a encomenda estar entre as últimas 500 listadas.

A consulta direta informa uma mensagem específica quando o código não existe. As informações exibidas vêm do SQL Server; o histórico pode estar vazio para registros antigos que foram criados antes da gravação de movimentações ser implementada.
