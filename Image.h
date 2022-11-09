#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <stb_image.h>
#include <stb_image_write.h>
#include <glm.hpp>

namespace Mid
{
	struct Image
	{
	public:
		std::string mimeType;
		int width = -1;
		int height = -1;
		std::vector<uint8_t> pixels;
		std::vector<uint8_t> code;

		glm::u8vec4 Get(int x, int y) const;
		glm::u8vec4 Get(int x, int y, int width, int height) const;

		void Set(int x, int y, const glm::u8vec4& v);

		void Load(const char* fn);

		void encode_png()
		{
			this->mimeType = "image/png";
			stbi_write_png_to_func([](void* context, void* data, int size)
				{
					std::vector<unsigned char>* buf = (std::vector<unsigned char>*)context;
					size_t offset = buf->size();
					buf->resize(offset + size);
					memcpy(buf->data() + offset, data, size);
				}, &code, this->width, this->height, 4, pixels.data(), this->width * 4);

		}

		void encode_jpeg()
		{
			this->mimeType = "image/jpeg";

			stbi_write_jpg_to_func([](void* context, void* data, int size)
				{
					std::vector<uint8_t>* buf = (std::vector<uint8_t>*)context;
					size_t offset = buf->size();
					buf->resize(offset + size);
					memcpy(buf->data() + offset, data, size);
				}, &code, this->width, this->height, 4, pixels.data(), 80);
		}

		void CreateRGBA(const Image& img_rgb, const Image& img_a);
		void CreateMR(const Image& img_metallic, const Image& img_roughness);
		void CreateSG(const Image& img_specular, const Image& img_roughness, float roughness);
	};

	glm::u8vec4 Image::Get(int x, int y) const
	{
		if (width < 0 || height < 0) return { 255,255,255,255 };
		const uint8_t* p = pixels.data() + (x + y * width) * 4;
		return { p[0], p[1], p[2], p[3] };
	}

	glm::u8vec4 Image::Get(int x, int y, int width, int height) const
	{
		if (this->width < 0 || this->height < 0) return { 255,255,255,255 };

		float fx = ((float)x + 0.5f) / (float)width;
		float fy = ((float)y + 0.5f) / (float)height;
		fx = fx * this->width - 0.5f;
		fy = fy * this->height - 0.5f;
		int ix = (int)floor(fx);
		int iy = (int)floor(fy);
		float fracX = fx - (float)ix;
		float fracY = fy - (float)iy;

		if (ix < 0) ix += this->width;
		if (iy < 0) iy += this->height;

		if (fracX == 0.0f && fracY == 0.0f)
		{
			return Get(ix, iy);
		}

		int ix2 = (ix + 1) % this->width;
		int iy2 = (iy + 1) % this->height;

		glm::u8vec4 v0 = Get(ix, iy);
		glm::u8vec4 v1 = Get(ix2, iy);
		glm::u8vec4 v2 = Get(ix, iy2);
		glm::u8vec4 v3 = Get(ix2, iy2);

		glm::vec4 f0 = glm::vec4(v0) * (1.0f - fracX) + glm::vec4(v1) * fracX;
		glm::vec4 f1 = glm::vec4(v2) * (1.0f - fracX) + glm::vec4(v3) * fracX;
		
		return glm::u8vec4(f0 * (1.0f - fracY) + f1 * fracY + 0.5f);
	}

	void Image::Set(int x, int y, const glm::u8vec4& v)
	{
		uint8_t* p = pixels.data() + (x + y * width) * 4;
		p[0] = v[0];
		p[1] = v[1];
		p[2] = v[2];
		p[3] = v[3];
	}

	void Image::Load(const char* fn)
	{
		std::string filename = fn;
		std::string ext = filename.substr(filename.find_last_of(".") + 1);
		this->mimeType = "image/jpeg";
		if (ext == "png" || ext == "PNG")
		{
			this->mimeType = "image/png";
		}
		int width, height, chn;
		uint8_t* data = stbi_load(fn, &width, &height, &chn, 4);
		this->width = width;
		this->height = height;
		this->pixels.resize(width * height * 4);
		memcpy(this->pixels.data(), data, width * height * 4);
		stbi_image_free(data);

		FILE* fp = fopen(fn, "rb");
		fseek(fp, 0, SEEK_END);
		size_t size = ftell(fp);
		fseek(fp, 0, SEEK_SET);
		this->code.resize(size);
		fread(this->code.data(), 1, size, fp);
		fclose(fp);
	}

	void Image::CreateRGBA(const Image& img_rgb, const Image& img_a)
	{
		if (img_rgb.width >= 0 && img_rgb.height >= 0)
		{
			this->width = img_rgb.width;
			this->height = img_rgb.height;
		}
		else
		{
			this->width = img_a.width;
			this->height = img_a.height;
		}

		this->pixels.resize(this->width * this->height * 4);

		for (int i = 0; i < this->height; i++)
		{
			for (int j = 0; j < this->width; j++)
			{				
				glm::u8vec4 rgb = img_rgb.Get(j, i, this->width, this->height);
				glm::u8vec4 a = img_a.Get(j, i, this->width, this->height);				
				this->Set(j, i, { rgb[0], rgb[1], rgb[2], a[0]});
			}
		}

		if (img_a.width >= 0 && img_a.height >= 0)
		{
			encode_png();
		}
		else
		{
			encode_jpeg();
		}
	}

	void Image::CreateMR(const Image& img_metallic, const Image& img_roughness)
	{
		if (img_metallic.width > this->width)
		{
			this->width = img_metallic.width;
		}

		if (img_metallic.height > this->height)
		{
			this->height = img_metallic.height;
		}

		if (img_roughness.width > this->width)
		{
			this->width = img_roughness.width;
		}

		if (img_roughness.height > this->height)
		{
			this->height = img_roughness.height;
		}

		this->pixels.resize(this->width * this->height * 4);

		for (int i = 0; i < this->height; i++)
		{
			for (int j = 0; j < this->width; j++)
			{
				glm::u8vec4 m = img_metallic.Get(j, i, this->width, this->height);
				glm::u8vec4 r = img_roughness.Get(j, i, this->width, this->height);
				this->Set(j, i, { 255, r[1], m[2], 255 });
			}
		}

		encode_jpeg();
		
	}

	void Image::CreateSG(const Image& img_specular, const Image& img_roughness, float roughness)
	{
		if (img_specular.width >= 0 && img_specular.height >= 0)
		{
			this->width = img_specular.width;
			this->height = img_specular.height;
		}
		else
		{
			this->width = img_roughness.width;
			this->height = img_roughness.height;
		}

		this->pixels.resize(this->width * this->height * 4);

		for (int i = 0; i < this->height; i++)
		{
			for (int j = 0; j < this->width; j++)
			{
				unsigned char glossiness = 255;
				glm::u8vec4 s = img_specular.Get(j, i, this->width, this->height);
				unsigned char r = (unsigned char)((float)img_roughness.Get(j, i, this->width, this->height)[0] * roughness + 0.5f);				
				this->Set(j, i, { s[0], s[1], s[2], 255 -r });
			}
		}

		encode_png();
	}


}
