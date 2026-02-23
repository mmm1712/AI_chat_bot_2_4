export default {
  async fetch(request, env) {
    const url = new URL(request.url);

    const token = request.headers.get("X-Auth") || "";
    if (!env.AUTH_TOKEN || token !== env.AUTH_TOKEN) {
      return new Response("Unauthorized", { status: 401 });
    }

    if (request.method !== "POST") {
      return new Response("POST only", { status: 405 });
    }

    if (url.pathname !== "/api/generate") {
      return new Response("Not found", { status: 404 });
    }

    let body;
    try {
      body = await request.json();
    } catch {
      return Response.json({ error: "bad json" }, { status: 400 });
    }

    const model = body.model || "@cf/meta/llama-3.1-8b-instruct";
    const prompt = body.prompt || "Reply in English with a full answer.";

    const result = await env.AI.run(model, {
      prompt,
      max_tokens: 300,
    });

    return Response.json({
      response: result?.response ?? "",
      stream: false
    });
  }
};
