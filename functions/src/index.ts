/*============= ESM ============*/
import * as v2 from "firebase-functions/v2";
import * as logger from "firebase-functions/logger";

type Indexable = { [key: string]: string };

export const helloWorld = v2.https.onRequest((request, response) => {
    // Lấy tên từ query string: ?name=lamp
    const name = (request.query.name as string) || "lamp";

    const items: Indexable = { lamp: 'this is lamp', chair: 'good chair' };
    const message = items[name] || "Item not found";

    logger.info(`Serving item: ${name} -> ${message}`);
    response.send(`<h1>${message}</h1>`);
});
